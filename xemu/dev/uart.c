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

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  uint8_t dll, dlm;
  uint8_t ier, iir, fcr, lcr, mcr, lsr, msr, scr;
  int has_rx;
  uint8_t rx;
  int thre_pending;   /* transmitter-empty interrupt latched in IIR */
} Uart;

static Uart uart[2];
static char serbuf[512];
static size_t serbuf_used;
const char *xemu_uart_serbuf(void) { return serbuf; }

void xemu_uart_reset(void) {
  memset(uart, 0, sizeof(uart));
  for (int i = 0; i < 2; i++) {
    uart[i].lsr = 0x60;   /* THRE | TEMT: transmitter empty */
    uart[i].msr = 0xB0;   /* CTS|DSR|DCD asserted */
    uart[i].iir = 0x01;   /* no interrupt pending */
  }
}

/* Poll stdin for a new byte (non-blocking).
 *
 * Called from the instruction loop, so it is deliberately cheap: the TTY is
 * only touched every XEMU_UART_POLL_INTERVAL instructions and only when the
 * guest has actually enabled receive interrupts. Reading every instruction
 * costs a syscall per instruction and dominates runtime. */
#define XEMU_UART_POLL_INTERVAL 1024ull

void xemu_uart_poll(void) {
  static uint64_t tick;
  static int stdin_nonblock;
  if (!xemu_console_serial) return;
  if (uart[0].has_rx) return;
  if ((uart[0].ier & 0x01) == 0) return;   /* nobody is listening */
  if ((++tick % XEMU_UART_POLL_INTERVAL) != 0) return;
  /* stdin may be a terminal or a pipe with nothing in it: a blocking read
     here freezes the whole emulator until the host sends a byte (the guest
     only enables RX interrupts once, so the banner never appears and the
     console looks hung).  Make it non-blocking once. */
  if (!stdin_nonblock) {
    int fl = fcntl(0, F_GETFL);
    if (fl >= 0) fcntl(0, F_SETFL, fl | O_NONBLOCK);
    stdin_nonblock = 1;
  }
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
    case 2:
      /* Reading IIR acknowledges the highest-priority pending interrupt.
         A pending THRE interrupt reads as 0x02 and clears on this read. */
      v = u->iir;
      if (u->iir == 0x02) {
        u->iir = (u->has_rx && (u->ier & 0x01)) ? 0x04 : 0x01;
        u->thre_pending = 0;
      }
      break;
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
        if (xemu_serial_stop) {
          if (serbuf_used < sizeof(serbuf) - 1) serbuf[serbuf_used++] = (char)b;
          else {
            memmove(serbuf, serbuf + 256, sizeof(serbuf) - 256);
            serbuf_used = sizeof(serbuf) - 256;
          }
          serbuf[serbuf_used] = 0;
          if (strstr(serbuf, xemu_serial_stop)) {
            xemu_serial_stopped = 1;
            xemu.halted = 1;
          }
        }
      }
      u->lsr |= 0x60;
      /* The byte leaves the shift register almost immediately at emulator
         speed, so latch THRE and, if the guest enabled the transmit-holding
         interrupt, raise IRQ4. Without this the tty layer pushes one byte
         into the FIFO, waits for the transmit interrupt and never resumes,
         which is why only the first byte of a userspace write() appeared. */
      u->thre_pending = 1;
      if (u->ier & 0x02) {
        u->iir = 0x02;
        xemu_pic_raise(4);
      }
      break;
    case 1:
      u->ier = b;
      if ((b & 0x02) && u->thre_pending) {
        u->iir = 0x02;
        xemu_pic_raise(4);
      }
      break;
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
