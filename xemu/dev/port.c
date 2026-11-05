/* Port I/O dispatcher: routes IN/OUT to the L0 device models. */
#include "../xemu.h"

#include <stdio.h>

/* xemu/dev/pci.c (L4): PCI mechanism #1 configuration space. xemu.h is owned
 * elsewhere this round, so the entry points are declared locally. They take
 * the absolute port number because both the register window (0xCF8 vs 0xCFC)
 * and the byte lane within it matter. */
int xemu_pci_in(uint32_t port, int size, uint64_t *val);
int xemu_pci_out(uint32_t port, int size, uint64_t val);
#define XEMU_PORT_PCI_CONFIG 0xCF8u   /* CONFIG_ADDRESS .. CONFIG_DATA+3 */

int xemu_console_serial = 1;
int xemu_trace_port = 0;
uint64_t xemu_port_out_count = 0;
uint64_t xemu_port_in_count = 0;

int xemu_port_in(uint32_t port, int size, uint64_t *val) {
  xemu_port_in_count++;
  if (xemu_trace_port)
    fprintf(stderr, "[port] in  %u (%d bits) rip=0x%llx\n",
            port, size * 8, (unsigned long long)xemu.rip);
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
  if (port >= XEMU_PORT_PCI_CONFIG && port <= XEMU_PORT_PCI_CONFIG + 7) {
    return xemu_pci_in(port, size, val);
  }
  /* Unknown port: behave like an absent device. */
  *val = (size >= 8) ? ~0ull : ((1ull << (size * 8)) - 1);
  return 0;
}

int xemu_port_out(uint32_t port, int size, uint64_t val) {
  xemu_port_out_count++;
  if (xemu_trace_port)
    fprintf(stderr, "[port] out %u, 0x%llx (%d bits) rip=0x%llx\n",
            port, (unsigned long long)val, size * 8,
            (unsigned long long)xemu.rip);
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
  if (port >= XEMU_PORT_PCI_CONFIG && port <= XEMU_PORT_PCI_CONFIG + 7) {
    return xemu_pci_out(port, size, val);
  }
  /* Writing an absent port is a no-op. */
  return 0;
}
