/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 tongxiaojun
 */
#include "xemu.h"
#include <string.h>

XemuState xemu;
uint8_t xemu_mem[XEMU_MEM_SIZE];

void xemu_reset(void) {
  /* Default IA32_PAT: WB WT UC- UC WB WT UC- UC. */
  xemu.pat = 0x0007040600070406ull;
  xemu_tlb_flush();
  xemu.dr[6] = 0xffff0ff0ull;
  xemu.dr[7] = 0x00000400ull;
  memset(&xemu, 0, sizeof(xemu));
  xemu_exit_code = 0;
  memset(xemu_mem, 0, sizeof(xemu_mem));
  xemu.rflags = 0x202;
  xemu.cs = 0x8;
  xemu.ss = 0x10;
  xemu_uart_reset();
  xemu_pic_reset();
  xemu_pit_reset();
}

/* Serial-console stop marker (see --serial-stop in main.c). */
const char *xemu_serial_stop = NULL;
int xemu_serial_stopped = 0;
