/* 16550A UART (COM1/COM2) for the L0 device model.
 *
 * Register offsets from the port base:
 *   0  THR/RBR/DLL   1  IER/DLM   2  IIR/FCR   3  LCR
 *   4  MCR           5  LSR       6  MSR       7  SCR
 *
 * Transmit bytes go to stdout when `xemu_console_serial` is set; received
 * bytes come from stdin in a non-blocking fashion so a guest can poll LSR.DR.
 */
#include "../xemu.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  uint8_t dll, dlm;
  uint8_t ier, iir, fcr, lcr, mcr, lsr, msr, scr;
  int has_rx;
  uint8_t rx;
  int tx_irq_armed;
} Uart;

static Uart uart[2];

void xemu_uart_reset(void) {
  memset(uart, 0, sizeof(uart));
  for (int i = 0; i < 2; i++) {
    uart[i].lsr = 0x60;   /* THRE | TEMT: transmitter empty */
    uart[i].msr = 0xB0;   /* CTS|DSR|DCD asserted */
    uart[i].iir = 0x01;   /* no interrupt pending */
  }
}

/* Poll stdin for a new byte (non-blocking). Called once per instruction. */
void xemu_uart_poll(void) {
  if (!xemu_console_serial) return;
  if (uart[0].has_rx) return;
  unsigned char c;
  if (read(0, &c, 1) == 1) {
    uart[0].rx = c;
    uart[0].has_rx = 1;
    uart[0].lsr |= 0x01;      /* DR */
    uart[0].iir = 0x04;       /* received data available */
    if (uart[0].ier & 0x01) xemu_pic_raise(4);
  }
}

static int dlab(const Uart *u) { return (u->lcr & 0x80) != 0; }

int xemu_uart_in(uint32_t off, int size, uint64_t *val) {
  Uart *u = &uart[0];
  uint64_t v = 0;
  switch (off & 7) {
    case 0:
      if (dlab(u)) { v = u->dll; break; }
      v = u->has_rx ? u->rx : 0;
      if (u->has_rx) {
        u->has_rx = 0;
        u->lsr &= (uint8_t)~0x01;
        u->iir = 0x01;
      }
      break;
    case 1: v = dlab(u) ? u->dlm : u->ier; break;
    case 2: v = u->iir; break;
    case 3: v = u->lcr; break;
    case 4: v = u->mcr; break;
    case 5: v = u->lsr; break;
    case 6: v = u->msr; break;
    case 7: v = u->scr; break;
    default: break;
  }
  *val = v;
  (void)size;
  return 0;
}

int xemu_uart_out(uint32_t off, int size, uint64_t val) {
  Uart *u = &uart[0];
  uint8_t b = (uint8_t)val;
  switch (off & 7) {
    case 0:
      if (dlab(u)) { u->dll = b; break; }
      if (xemu_console_serial) {
        /* Translate bare LF to CRLF for terminal output. */
        if (b == '\n') { fputc('\r', stdout); }
        fputc(b, stdout);
        fflush(stdout);
      }
      u->lsr |= 0x60;
      break;
    case 1: u->ier = b; break;
    case 2:
      if (b & 0x01) break;           /* FCR read-back is via IIR */
      u->fcr = b;
      if (b & 0x02) u->has_rx = 0;
      break;
    case 3: u->lcr = b; break;
    case 4: u->mcr = b; break;
    case 5: u->lsr = (uint8_t)((u->lsr & 0x01) | 0x60); break;
    case 6: u->msr = b & 0xF0; break;
    case 7: u->scr = b; break;
    default: break;
  }
  (void)size;
  return 0;
}
