/* NEMU-style difftest ABI for XEMU. */
#include "xemu.h"
#include "../ref/difftest_abi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int skip_ref = 0;

void difftest_init(int port) {
  (void)port;
  xemu_reset();
  skip_ref = 0;
}

void difftest_memcpy(uint64_t addr, void *buf, size_t n, int direction) {
  int rc;
  if (direction == DIFFTEST_TO_REF)
    rc = xemu_mem_write(addr, buf, n);
  else
    rc = xemu_mem_read(addr, buf, n);
  if (rc != 0) {
    fprintf(stderr, "[xemu] memcpy failed at 0x%llx (%zu bytes)\n",
            (unsigned long long)addr, n);
    abort();
  }
}

void difftest_regcpy(void *dut, int direction) {
  struct diff_state *s = (struct diff_state *)dut;
  if (direction == DIFFTEST_TO_REF) {
    memcpy(xemu.gpr, s->gpr, sizeof(xemu.gpr));
    xemu.rip = s->rip;
    xemu.rflags = s->rflags;
    memcpy(xemu.xmm, s->xmm, sizeof(xemu.xmm));
  } else {
    memcpy(s->gpr, xemu.gpr, sizeof(s->gpr));
    s->rip = xemu.rip;
    s->rflags = xemu.rflags;
    memcpy(s->xmm, xemu.xmm, sizeof(s->xmm));
  }
}

void difftest_exec(uint64_t n) {
  if (skip_ref) { skip_ref = 0; return; }
  for (uint64_t k = 0; k < n; k++) {
    XemuInst inst;
    int rc = xemu_decode(xemu.rip, &inst);
    if (rc != 0) {
      fprintf(stderr, "[xemu] decode failed at rip=0x%llx (rc=%d)\n",
              (unsigned long long)xemu.rip, rc);
      abort();
    }
    rc = xemu_exec(&inst);
    if (rc != 0) {
      fprintf(stderr, "[xemu] exec failed at rip=0x%llx op=%u\n",
              (unsigned long long)xemu.rip, (unsigned)inst.op);
      abort();
    }
    if (xemu.halted) break;
  }
}

void difftest_skip_ref(void) { skip_ref = 1; }

void difftest_raise_intr(uint64_t NO) {
  fprintf(stderr, "[xemu] raise_intr(%llu) not implemented in M0\n",
          (unsigned long long)NO);
  abort();
}
