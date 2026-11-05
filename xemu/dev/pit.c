/* 8254 programmable interval timer.
 *
 * Channel 0 drives IRQ0. Modes implemented: 2 (rate generator) and 3 (square
 * wave); mode 0 (interrupt on terminal count) is treated as one-shot.
 * The input clock is 1.193182 MHz, matching the PC/AT.
 */
#include "../xemu.h"

#include <string.h>

#define PIT_HZ 1193182ull

typedef struct {
  uint16_t reload;
  uint16_t counter;
  uint8_t mode;
  uint8_t access;      /* 1 = LSB, 2 = MSB, 3 = LSB then MSB */
  uint8_t latch_write;
  uint8_t latch_read;
  int have_lsb;
  uint8_t lsb;
  int writing;         /* expecting new reload */
  uint8_t ctrl;
} PitChan;

static PitChan ch[3];
static uint64_t ticks;      /* instructions since the last PIT tick */
static uint64_t div_accum;

/* Instructions per emulated second; the PIT clock is divided down from this. */
uint64_t xemu_pit_ips = 1000000ull;

void xemu_pit_reset(void) {
  memset(ch, 0, sizeof(ch));
  ticks = 0;
  div_accum = 0;
}

int xemu_pit_in(uint32_t off, int size, uint64_t *val) {
  uint32_t reg = off & 3;
  uint64_t v = 0;
  (void)size;
  if (reg == 3) { *val = 0; return 0; }
  PitChan *c = &ch[reg];
  /* Reading the counter latches it; return LSB then MSB. */
  if (!c->latch_read) {
    c->latch_read = c->latch_write ? c->latch_write : 3;
    c->latch_write = 0;
    uint16_t cur = c->counter ? c->counter : c->reload;
    c->lsb = (uint8_t)(cur & 0xff);
    c->have_lsb = 1;
    v = c->lsb;
  } else {
    uint16_t cur = c->counter ? c->counter : c->reload;
    v = (uint8_t)(cur >> 8);
    c->latch_read = 0;
  }
  *val = v;
  return 0;
}

int xemu_pit_out(uint32_t off, int size, uint64_t val) {
  uint32_t reg = off & 3;
  uint8_t b = (uint8_t)val;
  (void)size;
  if (reg == 3) {
    uint8_t chan = (b >> 6) & 3;
    uint8_t access = (b >> 4) & 3;
    uint8_t mode = (b >> 1) & 7;
    if (chan == 3) return 0;
    PitChan *c = &ch[chan];
    if (access == 0) {            /* counter latch command */
      c->latch_write = c->latch_write ? c->latch_write : 3;
      return 0;
    }
    c->ctrl = b;
    c->mode = (uint8_t)mode;
    c->access = access;
    c->writing = 1;
    c->have_lsb = 0;
    return 0;
  }
  PitChan *c = &ch[reg];
  if (!c->writing) return 0;
  if (c->access == 1) {           /* LSB only */
    c->reload = (uint16_t)((c->reload & 0xff00) | b);
    c->counter = c->reload;
    c->writing = 0;
  } else if (c->access == 2) {     /* MSB only */
    c->reload = (uint16_t)((c->reload & 0x00ff) | (b << 8));
    c->counter = c->reload;
    c->writing = 0;
  } else {                        /* LSB then MSB */
    if (!c->have_lsb) {
      c->lsb = b;
      c->have_lsb = 1;
    } else {
      c->reload = (uint16_t)((b << 8) | c->lsb);
      c->counter = c->reload;
      c->have_lsb = 0;
      c->writing = 0;
    }
  }
  return 0;
}

/* Advance the timer by one instruction. The interpreter runs roughly
 * `XEMU_PIT_IPS` instructions per emulated second (default 1 MIPS), so the
 * 1.193182 MHz input clock is divided down accordingly. */
/* Called once per instruction, so the common path (channel 0 idle, or still
 * counting down with nothing to do) must be a couple of integer ops.  The PIT
 * clock (1.193182 MHz) is one to two orders of magnitude slower than the
 * instruction rate, so the divide only runs when a tick can actually occur. */
void xemu_pit_tick(void) {
  ticks++;
  if (ch[0].reload == 0 || ch[0].counter == 0) return;
  uint64_t ips = xemu_pit_ips ? xemu_pit_ips : 1000000ull;
  div_accum += PIT_HZ;
  /* Fast exit: not enough accumulated clock to reach the next whole tick. */
  if (div_accum < ips) return;
  while (div_accum >= ips) {
    div_accum -= ips;
    if (ch[0].counter) ch[0].counter--;
    if (ch[0].counter == 0) {
      xemu_pic_raise(0);
      if (ch[0].mode == 2 || ch[0].mode == 3) ch[0].counter = ch[0].reload;
      else ch[0].counter = 0;      /* mode 0: one shot */
    }
  }
}
