#include "xemu.h"
#include <string.h>

int xemu_mem_read(uint64_t addr, void *buf, size_t n) {
  if (addr + n <= XEMU_MEM_SIZE) {
    memcpy(buf, &xemu_mem[addr], n);
    return 0;
  }
  if (addr >= XEMU_MMIO_HALT && addr + n <= XEMU_MMIO_HALT + XEMU_MMIO_SIZE) {
    memset(buf, 0, n);
    return 0;
  }
  return -1;
}

int xemu_mem_write(uint64_t addr, const void *buf, size_t n) {
  if (addr + n <= XEMU_MEM_SIZE) {
    memcpy(&xemu_mem[addr], buf, n);
    return 0;
  }
  if (addr >= XEMU_MMIO_HALT && addr + n <= XEMU_MMIO_HALT + XEMU_MMIO_SIZE) {
    /* Halt MMIO: any write stops the reference model. */
    xemu.halted = 1;
    return 0;
  }
  return -1;
}
