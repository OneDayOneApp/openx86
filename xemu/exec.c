#include "xemu.h"

int xemu_exec(const XemuInst *inst) {
  switch (inst->op) {
    case XOP_MOV:
      if (inst->a_kind != XK_GPR) return -1;
      xemu.gpr[inst->a_idx] = inst->imm;
      xemu.rip += inst->len;
      return 0;
    case XOP_HLT:
      xemu.halted = 1;
      xemu.rip += inst->len;
      return 0;
    default:
      return -1;
  }
}
