/* 8259A programmable interrupt controller (master + slave).
 *
 * Master at 0x20/0x21, slave at 0xA0/0xA1. Supports ICW1-ICW4, OCW1 (mask),
 * OCW2 (EOI) and OCW3 (read IRR/ISR). Vectors default to 0x20 (master) and
 * 0x28 (slave), matching the PC/AT layout Linux expects without APIC.
 */
#include "../xemu.h"

#include <string.h>

typedef struct {
  uint8_t irr, isr, imr;
  uint8_t vector_base;
  int init_step;      /* 0 = ready, 1..4 = expecting ICWn */
  uint8_t icw1;
  int read_isr;       /* OCW3: 0 = IRR, 1 = ISR */
  int poll_mode;
  uint8_t in_service; /* highest priority in-service bit */
} Pic;

static Pic pic[2];

void xemu_pic_reset(void) {
  memset(pic, 0, sizeof(pic));
  pic[0].vector_base = 0x20;
  pic[1].vector_base = 0x28;
  pic[0].imr = 0xFF;
  pic[1].imr = 0xFF;
}

int xemu_pic_in(uint32_t off, int size, uint64_t *val) {
  int idx = (off >= 0x80) ? 1 : 0;   /* 0xA0/0xA1 -> slave */
  uint32_t reg = off & 1;
  Pic *p = &pic[idx];
  uint64_t v;
  if (reg == 0) {
    if (p->poll_mode) {
      p->poll_mode = 0;
      v = 0x80;                       /* no interrupt pending */
    } else {
      v = p->read_isr ? p->isr : p->irr;
    }
    p->read_isr = 0;
  } else {
    v = p->imr;
  }
  *val = v;
  (void)size;
  return 0;
}

int xemu_pic_out(uint32_t off, int size, uint64_t val) {
  int idx = (off >= 0x80) ? 1 : 0;
  uint32_t reg = off & 1;
  uint8_t b = (uint8_t)val;
  Pic *p = &pic[idx];
  (void)size;

  if (reg == 0) {
    if (b & 0x10) {                   /* ICW1: start initialization */
      /* ICW1 does not touch the interrupt mask: the IMR keeps whatever
         state it had until OCW1 is written. Clearing it here unmasked every
         line during the kernel's ICW1..ICW4 sequence, which delivered IRQ0
         into an IDT entry that was not installed yet. */
      p->icw1 = b;
      p->init_step = 1;
      p->isr = 0;
      return 0;
    }
    if ((b & 0x18) == 0x00) {         /* OCW2: EOI */
      uint8_t level = b & 7;          /* OCW2 level field is bits 2:0 */
      if (b & 0x20) {
        if (b & 0x04) p->isr &= (uint8_t)~(1u << level);
        else          p->isr = 0;     /* specific/rotate EOI fallback */
      } else {
        p->isr &= (uint8_t)~(1u << p->in_service);
      }
      return 0;
    }
    if ((b & 0x18) == 0x08) {         /* OCW3 */
      if (b & 0x02) p->read_isr = (b & 0x01) != 0;
      if (b & 0x04) p->poll_mode = 1;
      return 0;
    }
    return 0;
  }

  /* Port+1: ICW2..ICW4 while initializing, OCW1 mask otherwise. */
  if (p->init_step == 1) {
    p->vector_base = b & 0xF8;
    p->init_step = (p->icw1 & 0x02) ? 2 : 3;  /* skip ICW3 for single mode */
    if (!(p->icw1 & 0x02)) p->init_step = 3;
    else p->init_step = 2;
    return 0;
  }
  if (p->init_step == 2) { p->init_step = 3; return 0; }    /* ICW3 */
  if (p->init_step == 3) { p->init_step = 4; return 0; }    /* ICW4 */
  if (p->init_step == 4) { p->init_step = 0; return 0; }
  p->imr = b;
  return 0;
}

/* Raise an IRQ line; the controller latches it in IRR and mirrors pending
 * slave interrupts into the master's IRQ2. Delivery is handled by
 * xemu_timer_tick()/the exec loop via xemu_pic_deliver(). */
void xemu_pic_raise(int irq) {
  if (irq < 0 || irq > 15) return;
  int idx = irq >= 8 ? 1 : 0;
  pic[idx].irr |= (uint8_t)(1u << (irq & 7));
  if (idx == 1) pic[0].irr |= 0x04;   /* cascade on IRQ2 */
}

/* Pick the highest-priority unmasked pending IRQ and acknowledge it.
 * Returns -1 when nothing is deliverable. */
int xemu_pic_ack(void) {
  static const int prio[8] = {0, 1, 2, 3, 4, 5, 6, 7};
  for (int i = 0; i < 8; i++) {
    int bit = prio[i];
    if (!(pic[0].irr & (1u << bit))) continue;
    if (pic[0].imr & (1u << bit)) continue;
    if (pic[0].isr) continue;        /* no nesting in this simple model */
    if (bit == 2) {                  /* cascade to slave */
      for (int j = 0; j < 8; j++) {
        if (!(pic[1].irr & (1u << j))) continue;
        if (pic[1].imr & (1u << j)) continue;
        if (pic[1].isr) continue;
        pic[1].irr &= (uint8_t)~(1u << j);
        pic[1].isr |= (uint8_t)(1u << j);
        pic[1].in_service = (uint8_t)j;
        pic[0].irr &= (uint8_t)~0x04;
        pic[0].isr |= 0x04;
        pic[0].in_service = 2;
        return pic[1].vector_base + j;
      }
      continue;
    }
    pic[0].irr &= (uint8_t)~(1u << bit);
    pic[0].isr |= (uint8_t)(1u << bit);
    pic[0].in_service = (uint8_t)bit;
    return pic[0].vector_base + bit;
  }
  return -1;
}

void xemu_pic_eoi_done(void) {
  /* Called when the guest writes OCW2; nothing extra to do. */
}
