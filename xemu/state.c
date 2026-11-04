#include "xemu.h"
#include <string.h>

XemuState xemu;
uint8_t xemu_mem[XEMU_MEM_SIZE];

void xemu_reset(void) {
  memset(&xemu, 0, sizeof(xemu));
  memset(xemu_mem, 0, sizeof(xemu_mem));
  xemu.rflags = 0x202;
  xemu.cs = 0x8;
  xemu.ss = 0x10;
}
