#ifndef X86_DIFFTEST_ABI_H
#define X86_DIFFTEST_ABI_H

#include <stdint.h>

/* NEMU/XiangShan-style difftest ABI.  Direction follows NEMU:
 *   0 = TO_DUT, 1 = TO_REF. */
#define DIFFTEST_TO_DUT 0
#define DIFFTEST_TO_REF 1

struct diff_state {
  uint64_t gpr[16];   /* RAX RCX RDX RBX RSP RBP RSI RDI R8..R15 */
  uint64_t rip;
  uint64_t rflags;
  uint64_t xmm[16][2]; /* low/high 64 bits, reserved for vector difftest */
};

#endif
