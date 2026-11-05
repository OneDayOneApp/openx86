/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 tongxiaojun
 */
// NEMU-style x86 reference model for DiffTest.
//
// Exports the same C ABI that XiangShan's difftest expects from NEMU:
//   difftest_init, difftest_memcpy, difftest_regcpy, difftest_exec,
//   difftest_skip_ref, difftest_raise_intr
//
// The instruction semantics are provided by Unicorn here; the ABI is what
// matters.  It can be replaced by a hand-written NEMU interpreter without
// touching the DUT-side difftest glue.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unicorn/unicorn.h>
#include "../ref/difftest_abi.h"

#define REF_MEM_SIZE   (16ull * 1024 * 1024)
#define MMIO_BASE      0xF0000000ull
#define MMIO_SIZE      0x1000ull

// Direction used by NEMU's difftest ABI.

static uc_engine *uc;
static int skip_ref = 0;
static uc_hook code_hook;
static int step_active = 0;
static uint64_t step_start = 0;

static void on_code(uc_engine *eng, uint64_t addr, uint32_t size, void *user) {
  (void)eng; (void)addr; (void)size; (void)user;
  if (!step_active) return;
  // Stop when control reaches a *different* instruction.  Self-modifying code
  // makes QEMU re-enter the same PC, which must not be mistaken for progress.
  if (addr != step_start) {
    step_active = 0;
    uc_emu_stop(eng);
  }
}

static const int xmm_regs[16] = {
  UC_X86_REG_XMM0, UC_X86_REG_XMM1, UC_X86_REG_XMM2, UC_X86_REG_XMM3,
  UC_X86_REG_XMM4, UC_X86_REG_XMM5, UC_X86_REG_XMM6, UC_X86_REG_XMM7,
  UC_X86_REG_XMM8, UC_X86_REG_XMM9, UC_X86_REG_XMM10, UC_X86_REG_XMM11,
  UC_X86_REG_XMM12, UC_X86_REG_XMM13, UC_X86_REG_XMM14, UC_X86_REG_XMM15,
};

static const int gpr_regs[16] = {
  UC_X86_REG_RAX, UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_RBX,
  UC_X86_REG_RSP, UC_X86_REG_RBP, UC_X86_REG_RSI, UC_X86_REG_RDI,
  UC_X86_REG_R8,  UC_X86_REG_R9,  UC_X86_REG_R10, UC_X86_REG_R11,
  UC_X86_REG_R12, UC_X86_REG_R13, UC_X86_REG_R14, UC_X86_REG_R15,
};

static void ref_abort(const char *what, uc_err err) {
  fprintf(stderr, "[ref] %s failed: %s\n", what, uc_strerror(err));
  abort();
}

void difftest_init(int port) {
  (void)port;
  if (uc) return;
  uc_err err = uc_open(UC_ARCH_X86, UC_MODE_64, &uc);
  if (err != UC_ERR_OK) ref_abort("uc_open", err);
  err = uc_mem_map(uc, 0, REF_MEM_SIZE, UC_PROT_ALL);
  if (err != UC_ERR_OK) ref_abort("map low memory", err);
  err = uc_mem_map(uc, MMIO_BASE, MMIO_SIZE, UC_PROT_ALL);
  if (err != UC_ERR_OK) ref_abort("map mmio", err);
  err = uc_hook_add(uc, &code_hook, UC_HOOK_CODE, (void *)on_code, NULL, 1, 0);
  if (err != UC_ERR_OK) ref_abort("hook code", err);
  skip_ref = 0;
}

void difftest_memcpy(uint64_t addr, void *buf, size_t n, int direction) {
  uc_err err;
  if (direction == DIFFTEST_TO_REF)
    err = uc_mem_write(uc, addr, buf, n);
  else
    err = uc_mem_read(uc, addr, buf, n);
  if (err != UC_ERR_OK) ref_abort("memcpy", err);
}

void difftest_regcpy(void *dut, int direction) {
  struct diff_state *s = (struct diff_state *)dut;
  if (direction == DIFFTEST_TO_REF) {
    for (int i = 0; i < 16; i++)
      uc_reg_write(uc, gpr_regs[i], &s->gpr[i]);
    for (int i = 0; i < 16; i++)
      uc_reg_write(uc, xmm_regs[i], &s->xmm[i][0]);
    uc_reg_write(uc, UC_X86_REG_RIP, &s->rip);
    uc_reg_write(uc, UC_X86_REG_EFLAGS, &s->rflags);
  } else {
    for (int i = 0; i < 16; i++)
      uc_reg_read(uc, gpr_regs[i], &s->gpr[i]);
    for (int i = 0; i < 16; i++)
      uc_reg_read(uc, xmm_regs[i], &s->xmm[i][0]);
    uc_reg_read(uc, UC_X86_REG_RIP, &s->rip);
    uc_reg_read(uc, UC_X86_REG_EFLAGS, &s->rflags);
  }
}

void difftest_exec(uint64_t n) {
  if (skip_ref) { skip_ref = 0; return; }
  uint64_t rip = 0;
  uc_reg_read(uc, UC_X86_REG_RIP, &rip);
  for (uint64_t i = 0; i < n; i++) {
    uc_reg_read(uc, UC_X86_REG_RIP, &rip);
    step_start = rip;
    step_active = 1;
    uc_err err = uc_emu_start(uc, rip, 0, 0, 0);
    if (getenv("REFTRACE")) {
      uint64_t after = 0, r8 = 0, rsp = 0; uc_reg_read(uc, UC_X86_REG_RIP, &after);
      uc_reg_read(uc, UC_X86_REG_R8, &r8);
      uc_reg_read(uc, UC_X86_REG_RSP, &rsp);
      unsigned char code[8] = {0};
      uc_mem_read(uc, rip, code, 8);
      uint64_t x4[2] = {0,0}; uc_reg_read(uc, UC_X86_REG_XMM4, &x4[0]);
      fprintf(stderr, "[ref] exec pc=0x%llx -> 0x%llx err=%d r8=0x%llx rsp=0x%llx xmm4=%016llx%016llx code=%02x%02x%02x%02x%02x%02x%02x%02x\n",
              (unsigned long long)rip, (unsigned long long)after, (int)err,
              (unsigned long long)r8, (unsigned long long)rsp,
              (unsigned long long)x4[1], (unsigned long long)x4[0],
              code[0],code[1],code[2],code[3],code[4],code[5],code[6],code[7]);
    }
    if (err != UC_ERR_OK) {
      fprintf(stderr, "[ref] exec at 0x%llx failed: %s\n",
              (unsigned long long)rip, uc_strerror(err));
      abort();
    }
  }
}

void difftest_skip_ref(void) {
  skip_ref = 1;
}

void difftest_raise_intr(uint64_t NO) {
  // Full NEMU semantics would push an interrupt frame and jump to the IDT
  // handler without executing the faulting instruction.  The DUT must signal
  // traps for that; this prototype only reports the request.
  fprintf(stderr, "[ref] raise_intr(%llu) not implemented\n",
          (unsigned long long)NO);
  abort();
}
