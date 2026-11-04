/* NEMU-style difftest ABI for XEMU. */
#include "xemu.h"
#include "../ref/difftest_abi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>

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

int difftest_halted(void) { return xemu.halted; }

void difftest_exec(uint64_t n) {
  if (skip_ref) { skip_ref = 0; return; }
  for (uint64_t k = 0; k < n; k++) {
    XemuInst inst;
    int rc = xemu_decode(xemu.rip, &inst);
    if (rc == 1) continue; /* fetch fault delivered */
    if (rc != 0) {
      fprintf(stderr, "[xemu] decode failed at rip=0x%llx (rc=%d)\n",
              (unsigned long long)xemu.rip, rc);
      abort();
    }
    uint64_t before = xemu.rflags;
    rc = xemu_exec(&inst);
    if (getenv("XEMUTRACE"))
      fprintf(stderr, "[xemu] pc=0x%llx op=%u fl=0x%llx->0x%llx rip->0x%llx\n",
              (unsigned long long)(xemu.rip - inst.len), (unsigned)inst.op,
              (unsigned long long)before, (unsigned long long)xemu.rflags,
              (unsigned long long)xemu.rip);
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
  xemu_raise_exc(NO, 0, xemu.rip);
}

/* Added for system-mode difftest: DUT supplies error code and faulting PC. */
void difftest_raise_intr_ex(uint64_t NO, uint64_t err, uint64_t pc) {
  xemu_raise_exc(NO, err, pc);
}
