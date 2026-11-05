/* Port I/O dispatcher: routes IN/OUT to the L0 device models. */
#include "../xemu.h"

#include <stdio.h>

int xemu_console_serial = 1;

int xemu_port_in(uint32_t port, int size, uint64_t *val) {
  if (port >= XEMU_PORT_UART && port <= XEMU_PORT_UART + 7) {
    return xemu_uart_in(port - XEMU_PORT_UART, size, val);
  }
  if (port >= XEMU_PORT_PIC_MASTER && port <= XEMU_PORT_PIC_MASTER + 1) {
    return xemu_pic_in(port - XEMU_PORT_PIC_MASTER, size, val);
  }
  if (port >= XEMU_PORT_PIC_SLAVE && port <= XEMU_PORT_PIC_SLAVE + 1) {
    return xemu_pic_in(port - XEMU_PORT_PIC_SLAVE + 0x80, size, val);
  }
  if (port >= XEMU_PORT_PIT && port <= XEMU_PORT_PIT + 3) {
    return xemu_pit_in(port - XEMU_PORT_PIT, size, val);
  }
  /* Unknown port: behave like an absent device. */
  *val = (size >= 8) ? ~0ull : ((1ull << (size * 8)) - 1);
  return 0;
}

int xemu_port_out(uint32_t port, int size, uint64_t val) {
  if (port >= XEMU_PORT_UART && port <= XEMU_PORT_UART + 7) {
    return xemu_uart_out(port - XEMU_PORT_UART, size, val);
  }
  if (port >= XEMU_PORT_PIC_MASTER && port <= XEMU_PORT_PIC_MASTER + 1) {
    return xemu_pic_out(port - XEMU_PORT_PIC_MASTER, size, val);
  }
  if (port >= XEMU_PORT_PIC_SLAVE && port <= XEMU_PORT_PIC_SLAVE + 1) {
    return xemu_pic_out(port - XEMU_PORT_PIC_SLAVE + 0x80, size, val);
  }
  if (port >= XEMU_PORT_PIT && port <= XEMU_PORT_PIT + 3) {
    return xemu_pit_out(port - XEMU_PORT_PIT, size, val);
  }
  /* Writing an absent port is a no-op. */
  return 0;
}
