/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 tongxiaojun
 */
#include "xemu.h"
#include "ops.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Immediate classes (mirror ImmKind). */
enum { IMM_NONE = 0, IMM_8 = 1, IMM_Z = 2, IMM_32 = 3, IMM_64 = 4, IMM_16 = 5,
       /* Address-sized immediate (MOV moffs): 8 bytes in 64-bit mode, 4 with a
          0x67 prefix, independent of the data width the instruction uses. */
       IMM_ADDR = 6 };
/* Decode forms (mirror DecodeForm). */
enum { FORM_NORMAL = 0, FORM_MOVZX8 = 1, FORM_MOVZX16 = 2, FORM_MOVSX8 = 3,
       FORM_MOVSX16 = 4, FORM_MOVSXD = 5, FORM_ACC = 6, FORM_SPECIAL = 7 };
/* Opcode groups (mirror OpcodeGroup).
 *
 * G_X87 + n covers the x87 escapes D8..DF, so it needs eight consecutive
 * values and must not overlap any other group.  It used to start at 8 with
 * G_15 = 9, which made `G_15 == G_X87 + 1 == D9`: the D9 escape was routed to
 * the 0F AE system table instead of x87_group(), so `fld st(i)` decoded as
 * FXSAVE, `fxch st(i)` as FXRSTOR and `fchs`/`fabs`/`ftst`/`fxam` as #UD. */
enum { G_NONE = 0, G_1 = 1, G_2 = 2, G_3 = 3, G_4 = 4, G_5 = 5, G_11 = 6, G_1A = 7,
       G_15 = 8,
       G_X87 = 9 };             /* G_X87 + n for opcodes D8+n */
/* SIMD kinds (mirror SimdKind). */
enum { SIMD_NONE = 0, SIMD_SS = 1, SIMD_SD = 2, SIMD_PS = 3, SIMD_PD = 4, SIMD_DQ = 5 };

typedef struct {
  /* `op` must be at least 16 bits wide: the opcode enum does not fit in a
     byte (the x87 block alone reaches 204, the XEMU-only expansion starts at
     300), and a truncated value silently aliases a different instruction. */
  uint16_t op;
  uint8_t modrm, imm, dir, group, force8, form, sse_sel, store;
  /* ST(i) operand for x87 register forms (mod == 3).  Kept out of `imm`,
     which is the *immediate encoding* field: storing an ST index there made
     the decoder add an immediate byte to the instruction length. */
  uint8_t st_idx;
  uint8_t has_st;
} DInfo;

/* Row index is `group - 1`, so G_15 (9) addresses row 8: nine rows, not
 * seven. Writing groups[G_15 - 1] used to run 72 bytes past the end of this
 * array and corrupt neighbouring .bss (the x87 state in fpu.c sat there). */
static DInfo primary[256], twobyte[256], groups[9][8];
static int tables_ready;

#define DI(opv, ...) ((DInfo){ .op = (opv), __VA_ARGS__ })

static void set(DInfo *t, int i, DInfo d) { t[i] = d; }

/* exec.c dispatches x87 with `op >= Ops_FNINIT && op < Ops_FXSAVE`.  If a
 * future opcode insertion moves the last x87 op past Ops_FXSAVE (or moves
 * Ops_FXSAVE down into the block) the register forms silently stop being
 * executed, which is exactly the class of bug this file just fixed. */
_Static_assert(Ops_FINCSTP < Ops_FXSAVE,
               "the last x87 opcode must stay below Ops_FXSAVE");
_Static_assert(Ops_FNINIT < Ops_FNOP,
               "the x87 block must be ordered");

static void init_tables(void) {
  if (tables_ready) return;
  for (int i = 0; i < 256; i++) {
    primary[i] = DI(Ops_UNDEF, .modrm = 0);
    twobyte[i] = DI(Ops_UNDEF, .modrm = 0);
  }
  static const int alu[8] = { Ops_ADD, Ops_OR, Ops_ADC, Ops_SBB,
                              Ops_AND, Ops_SUB, Ops_XOR, Ops_CMP };
  for (int b = 0; b < 0x40; b++) {
    int low = b & 7;
    if (low < 4) {
      primary[b] = DI(alu[(b >> 3) & 7], .modrm = 1, .dir = (b >> 1) & 1,
                      .force8 = (b & 1) == 0);
    } else if (low == 4 || low == 5) {
      primary[b] = DI(alu[(b >> 3) & 7], .modrm = 0,
                      .imm = (b & 1) ? IMM_Z : IMM_8, .dir = 1,
                      .force8 = (b & 1) == 0, .form = FORM_ACC);
    }
  }
  for (int r = 0; r < 8; r++) {
    primary[0x50 + r] = DI(Ops_PUSH, .modrm = 0, .dir = 1, .form = FORM_SPECIAL);
    primary[0x58 + r] = DI(Ops_POP, .modrm = 0, .dir = 1, .form = FORM_SPECIAL);
  }
  primary[0x63] = DI(Ops_MOVSXD, .modrm = 1, .dir = 1, .form = FORM_MOVSXD);
  primary[0x68] = DI(Ops_PUSH, .modrm = 0, .imm = IMM_Z, .dir = 1, .form = FORM_SPECIAL);
  primary[0x69] = DI(Ops_IMUL, .modrm = 1, .imm = IMM_Z, .dir = 1);
  primary[0x6a] = DI(Ops_PUSH, .modrm = 0, .imm = IMM_8, .dir = 1, .form = FORM_SPECIAL);
  primary[0x6b] = DI(Ops_IMUL, .modrm = 1, .imm = IMM_8, .dir = 1);
  for (int r = 0; r < 16; r++) primary[0x70 + r] = DI(Ops_JCC, .modrm = 0, .imm = IMM_8);
  /* AL/eAX, imm8/imm(Z) immediate forms: 0x04/0x05 ADD, 0x0C/0x0D OR,
     0x14/0x15 ADC, 0x1C/0x1D SBB, 0x24/0x25 AND, 0x2C/0x2D SUB,
     0x34/0x35 XOR, 0x3C/0x3D CMP. The accumulator is always operand A. */
  {
    static const uint16_t alu[8] = {Ops_ADD, Ops_OR, Ops_ADC, Ops_SBB,
                                   Ops_AND, Ops_SUB, Ops_XOR, Ops_CMP};
    for (int i = 0; i < 8; i++) {
      primary[0x04 + i * 8] = DI(alu[i], .modrm = 0, .imm = IMM_8, .force8 = 1);
      primary[0x05 + i * 8] = DI(alu[i], .modrm = 0, .imm = IMM_Z);
    }
  }
  primary[0x80] = DI(Ops_UNDEF, .modrm = 1, .group = G_1, .force8 = 1, .imm = IMM_8);
  primary[0x81] = DI(Ops_UNDEF, .modrm = 1, .group = G_1, .imm = IMM_Z);
  primary[0x83] = DI(Ops_UNDEF, .modrm = 1, .group = G_1, .imm = IMM_8);
  primary[0x84] = DI(Ops_TEST, .modrm = 1, .dir = 0, .force8 = 1);
  primary[0x85] = DI(Ops_TEST, .modrm = 1, .dir = 0);
  primary[0x86] = DI(Ops_XCHG, .modrm = 1, .dir = 0, .force8 = 1);
  primary[0x87] = DI(Ops_XCHG, .modrm = 1, .dir = 0);
  primary[0x88] = DI(Ops_MOV, .modrm = 1, .dir = 0, .force8 = 1);
  primary[0x89] = DI(Ops_MOV, .modrm = 1, .dir = 0);
  primary[0x8a] = DI(Ops_MOV, .modrm = 1, .dir = 1, .force8 = 1);
  primary[0x8b] = DI(Ops_MOV, .modrm = 1, .dir = 1);
  primary[0x8c] = DI(Ops_MOV_SEG, .modrm = 1, .dir = 0);
  primary[0x8d] = DI(Ops_LEA, .modrm = 1, .dir = 1);
  primary[0x8e] = DI(Ops_MOV_SEG, .modrm = 1, .dir = 1);
  primary[0x8f] = DI(Ops_UNDEF, .modrm = 1, .group = G_1A);
  primary[0x90] = DI(Ops_NOP, .modrm = 0);
  for (int r = 1; r < 8; r++) primary[0x90 + r] = DI(Ops_XCHG, .modrm = 0, .dir = 1, .form = FORM_SPECIAL);
  primary[0x98] = DI(Ops_CBW, .modrm = 0);
  primary[0x99] = DI(Ops_CDQ, .modrm = 0);
  primary[0x9b] = DI(Ops_FWAIT, .modrm = 0);
  primary[0x9c] = DI(Ops_PUSHF, .modrm = 0);
  primary[0x9d] = DI(Ops_POPF, .modrm = 0);
  primary[0x9e] = DI(Ops_SAHF, .modrm = 0);
  primary[0x9f] = DI(Ops_LAHF, .modrm = 0);
  primary[0xa4] = DI(Ops_MOVS, .modrm = 0, .force8 = 1);
  primary[0xa5] = DI(Ops_MOVS, .modrm = 0);
  primary[0xa6] = DI(Ops_CMPS, .modrm = 0, .force8 = 1);
  primary[0xa7] = DI(Ops_CMPS, .modrm = 0);
  primary[0xa8] = DI(Ops_TEST, .modrm = 0, .imm = IMM_8, .force8 = 1, .form = FORM_ACC);
  primary[0xa9] = DI(Ops_TEST, .modrm = 0, .imm = IMM_Z, .form = FORM_ACC);
  primary[0xaa] = DI(Ops_STOS, .modrm = 0, .force8 = 1);
  primary[0xab] = DI(Ops_STOS, .modrm = 0);
  primary[0xac] = DI(Ops_LODS, .modrm = 0, .force8 = 1);
  primary[0xad] = DI(Ops_LODS, .modrm = 0);
  primary[0xae] = DI(Ops_SCAS, .modrm = 0, .force8 = 1);
  primary[0xaf] = DI(Ops_SCAS, .modrm = 0);
  for (int r = 0; r < 8; r++) {
    primary[0xb0 + r] = DI(Ops_MOV, .modrm = 0, .imm = IMM_8, .dir = 1, .force8 = 1, .form = FORM_SPECIAL);
    primary[0xb8 + r] = DI(Ops_MOV, .modrm = 0, .imm = IMM_64, .dir = 1, .form = FORM_SPECIAL);
  }
  primary[0xc0] = DI(Ops_UNDEF, .modrm = 1, .group = G_2, .imm = IMM_8, .force8 = 1);
  primary[0xc1] = DI(Ops_UNDEF, .modrm = 1, .group = G_2, .imm = IMM_8);
  primary[0xc2] = DI(Ops_RET, .modrm = 0, .imm = IMM_16);
  primary[0xca] = DI(Ops_RETF, .modrm = 0, .imm = IMM_16);
  primary[0xcb] = DI(Ops_RETF, .modrm = 0);
  primary[0xc3] = DI(Ops_RET, .modrm = 0);
  primary[0xc6] = DI(Ops_UNDEF, .modrm = 1, .group = G_11, .imm = IMM_8, .force8 = 1);
  primary[0xc7] = DI(Ops_UNDEF, .modrm = 1, .group = G_11, .imm = IMM_Z);
  primary[0xc8] = DI(Ops_UNDEF, .modrm = 0, .imm = IMM_16);
  primary[0xc9] = DI(Ops_LEAVE, .modrm = 0);
  primary[0xc8] = DI(Ops_ENTER, .modrm = 0, .imm = IMM_16);
  primary[0xcc] = DI(Ops_INT3, .modrm = 0);
  primary[0xcd] = DI(Ops_INT, .modrm = 0, .imm = IMM_8);
  primary[0xcf] = DI(Ops_IRET, .modrm = 0);
  primary[0xd0] = DI(Ops_UNDEF, .modrm = 1, .group = G_2, .force8 = 1);
  primary[0xd1] = DI(Ops_UNDEF, .modrm = 1, .group = G_2);
  primary[0xd2] = DI(Ops_UNDEF, .modrm = 1, .group = G_2, .force8 = 1);
  primary[0xd3] = DI(Ops_UNDEF, .modrm = 1, .group = G_2);
  primary[0xd7] = DI(Ops_UNDEF, .modrm = 0);
  primary[0xe4] = DI(Ops_IN, .modrm = 0, .imm = IMM_8);
  primary[0xe5] = DI(Ops_IN, .modrm = 0, .imm = IMM_8);
  primary[0xe6] = DI(Ops_OUT, .modrm = 0, .imm = IMM_8);
  primary[0xe7] = DI(Ops_OUT, .modrm = 0, .imm = IMM_8);
  primary[0xec] = DI(Ops_IN, .modrm = 0);
  primary[0xed] = DI(Ops_IN, .modrm = 0);
  primary[0xee] = DI(Ops_OUT, .modrm = 0);
  primary[0xef] = DI(Ops_OUT, .modrm = 0);
  primary[0x6c] = DI(Ops_INS, .modrm = 0);
  primary[0x6d] = DI(Ops_INS, .modrm = 0);
  primary[0x6e] = DI(Ops_OUTS, .modrm = 0);
  primary[0x6f] = DI(Ops_OUTS, .modrm = 0);
  /* x87 escaped opcodes D8..DF. The ModRM byte selects the operation; the
     reg field picks the variant, and the r/m field either names ST(i) or a
     memory operand. */
  for (int i = 0; i < 8; i++) {
    primary[0xd8 + i] = DI(Ops_UNDEF, .modrm = 1, .group = (uint8_t)(G_X87 + i));
  }
  /* 0F AE: FXSAVE/FXRSTOR, LFENCE/MFENCE/SFENCE, CLFLUSH, MXCSR access. */
  {
    static const uint16_t g15[8] = {
      Ops_FXSAVE,    /* /0 FXSAVE  */
      Ops_FXRSTOR,   /* /1 FXRSTOR */
      Ops_LDMXCSR,   /* /2 LDMXCSR */
      Ops_STMXCSR,   /* /3 STMXCSR */
      Ops_UNDEF,     /* /4 XSAVE (unsupported) */
      Ops_LFENCE,    /* /5 LFENCE  */
      Ops_MFENCE,    /* /6 MFENCE  */
      Ops_CLFLUSH,   /* /7 CLFLUSH */
    };
    for (int i = 0; i < 8; i++) groups[G_15 - 1][i] = DI(g15[i], .modrm = 1);
  }
  set(twobyte, 0xae, DI(Ops_UNDEF, .modrm = 1, .group = G_15));
  primary[0xe0] = DI(Ops_LOOPNE, .modrm = 0, .imm = IMM_8);
  primary[0xe1] = DI(Ops_LOOPE, .modrm = 0, .imm = IMM_8);
  primary[0xe2] = DI(Ops_LOOP, .modrm = 0, .imm = IMM_8);
  primary[0xe3] = DI(Ops_JRCXZ, .modrm = 0, .imm = IMM_8);
  primary[0xe8] = DI(Ops_CALL, .modrm = 0, .imm = IMM_32);
  primary[0xe9] = DI(Ops_JMP, .modrm = 0, .imm = IMM_32);
  primary[0xeb] = DI(Ops_JMP, .modrm = 0, .imm = IMM_8);
  primary[0xf4] = DI(Ops_HLT, .modrm = 0);
  primary[0xf5] = DI(Ops_CMC, .modrm = 0);
  primary[0xf6] = DI(Ops_UNDEF, .modrm = 1, .group = G_3, .force8 = 1);
  primary[0xf7] = DI(Ops_UNDEF, .modrm = 1, .group = G_3);
  primary[0xf8] = DI(Ops_CLC, .modrm = 0);
  primary[0xf9] = DI(Ops_STC, .modrm = 0);
  primary[0xfa] = DI(Ops_CLI, .modrm = 0);
  primary[0xfb] = DI(Ops_STI, .modrm = 0);
  primary[0xfc] = DI(Ops_CLD, .modrm = 0);
  primary[0xfd] = DI(Ops_STD, .modrm = 0);
  primary[0xfe] = DI(Ops_UNDEF, .modrm = 1, .group = G_4, .force8 = 1);
  primary[0xff] = DI(Ops_UNDEF, .modrm = 1, .group = G_5);

  set(twobyte, 0x05, DI(Ops_SYSCALL, .modrm = 0));
  set(twobyte, 0x07, DI(Ops_SYSRET, .modrm = 0));
  set(twobyte, 0x20, DI(Ops_MOV_CR, .modrm = 1, .dir = 0));
  set(twobyte, 0x22, DI(Ops_MOV_CR, .modrm = 1, .dir = 1));
  /* Debug registers: 0F 21 reads DRn into a GPR, 0F 23 writes it. */
  set(twobyte, 0x21, DI(Ops_MOV_DR, .modrm = 1, .dir = 0));
  set(twobyte, 0x23, DI(Ops_MOV_DR, .modrm = 1, .dir = 1));
  set(twobyte, 0x30, DI(Ops_WRMSR, .modrm = 0));
  set(twobyte, 0x32, DI(Ops_RDMSR, .modrm = 0));
  set(twobyte, 0x0b, DI(Ops_UD2, .modrm = 0));
  set(twobyte, 0x01, DI(Ops_UNDEF, .modrm = 1));
  set(twobyte, 0x06, DI(Ops_CLTS, .modrm = 0));
  set(twobyte, 0x09, DI(Ops_WBINVD, .modrm = 0));
  set(twobyte, 0x77, DI(Ops_EMMS, .modrm = 0));
  set(twobyte, 0x18, DI(Ops_PREFETCH, .modrm = 1));
  set(twobyte, 0xb9, DI(Ops_UD1, .modrm = 1));
  set(twobyte, 0xa4, DI(Ops_SHLD, .modrm = 1, .imm = IMM_8));
  set(twobyte, 0xa5, DI(Ops_SHLD, .modrm = 1));
  set(twobyte, 0xac, DI(Ops_SHRD, .modrm = 1, .imm = IMM_8));
  set(twobyte, 0xad, DI(Ops_SHRD, .modrm = 1));
  set(twobyte, 0xb8, DI(Ops_POPCNT, .modrm = 1));
  set(twobyte, 0xc3, DI(Ops_MOVNTI, .modrm = 1, .dir = 0));
  set(twobyte, 0x02, DI(Ops_LAR, .modrm = 1, .dir = 1));
  set(twobyte, 0x31, DI(Ops_RDTSC, .modrm = 0));
  set(twobyte, 0x33, DI(Ops_RDPMC, .modrm = 0));
  for (int r = 0; r < 8; r++) set(twobyte, 0xc8 + r, DI(Ops_BSWAP, .modrm = 0));
  set(twobyte, 0x00, DI(Ops_UNDEF, .modrm = 1));
  set(twobyte, 0x1e, DI(Ops_ENDBR, .modrm = 1));
  set(twobyte, 0x1f, DI(Ops_NOP, .modrm = 1));
  set(twobyte, 0x10, DI(Ops_MOVPS, .modrm = 1, .dir = 1, .sse_sel = 1));
  set(twobyte, 0x11, DI(Ops_MOVPS, .modrm = 1, .dir = 0, .store = 1, .sse_sel = 1));
  set(twobyte, 0x28, DI(Ops_MOVPS, .modrm = 1, .dir = 1, .sse_sel = 1));
  set(twobyte, 0x29, DI(Ops_MOVPS, .modrm = 1, .dir = 0, .store = 1, .sse_sel = 1));
  set(twobyte, 0x2a, DI(Ops_CVTSI2SD, .modrm = 1, .dir = 1, .sse_sel = 1));
  set(twobyte, 0x2c, DI(Ops_CVTTSD2SI, .modrm = 1, .dir = 1, .sse_sel = 1));
  set(twobyte, 0x2d, DI(Ops_CVTSD2SI, .modrm = 1, .dir = 1, .sse_sel = 1));
  set(twobyte, 0x2e, DI(Ops_SIMD_UCOMI, .modrm = 1, .dir = 1, .sse_sel = 1));
  set(twobyte, 0x2f, DI(Ops_SIMD_COMI, .modrm = 1, .dir = 1, .sse_sel = 1));
  for (int r = 0; r < 16; r++) set(twobyte, 0x40 + r, DI(Ops_CMOVCC, .modrm = 1, .dir = 1));
  for (int r = 0; r < 16; r++) set(twobyte, 0x80 + r, DI(Ops_JCC, .modrm = 0, .imm = IMM_32));
  for (int r = 0; r < 16; r++) set(twobyte, 0x90 + r, DI(Ops_SETCC, .modrm = 1, .force8 = 1));
  set(twobyte, 0x51, DI(Ops_SIMD_SQRT, .modrm = 1, .dir = 1, .sse_sel = 1));
  set(twobyte, 0x54, DI(Ops_SIMD_AND, .modrm = 1, .dir = 1, .sse_sel = 1));
  set(twobyte, 0x55, DI(Ops_SIMD_ANDN, .modrm = 1, .dir = 1, .sse_sel = 1));
  set(twobyte, 0x56, DI(Ops_SIMD_OR, .modrm = 1, .dir = 1, .sse_sel = 1));
  set(twobyte, 0x34, DI(Ops_SYSENTER, .modrm = 0));
  set(twobyte, 0x35, DI(Ops_SYSEXIT, .modrm = 0));
  set(twobyte, 0x57, DI(Ops_SIMD_XOR, .modrm = 1, .dir = 1, .sse_sel = 1));
  set(twobyte, 0x58, DI(Ops_SIMD_ADD, .modrm = 1, .dir = 1, .sse_sel = 1));
  set(twobyte, 0x59, DI(Ops_SIMD_MUL, .modrm = 1, .dir = 1, .sse_sel = 1));
  set(twobyte, 0x5a, DI(Ops_CVTSS2SD, .modrm = 1, .dir = 1, .sse_sel = 1));
  set(twobyte, 0x5b, DI(Ops_CVTDQ2PS, .modrm = 1, .dir = 1));
  set(twobyte, 0x5c, DI(Ops_SIMD_SUB, .modrm = 1, .dir = 1, .sse_sel = 1));
  set(twobyte, 0x5d, DI(Ops_SIMD_MIN, .modrm = 1, .dir = 1, .sse_sel = 1));
  set(twobyte, 0x5e, DI(Ops_SIMD_DIV, .modrm = 1, .dir = 1, .sse_sel = 1));
  set(twobyte, 0x5f, DI(Ops_SIMD_MAX, .modrm = 1, .dir = 1, .sse_sel = 1));
  set(twobyte, 0x6e, DI(Ops_MOVD, .modrm = 1, .dir = 1, .sse_sel = 1));
  set(twobyte, 0x6f, DI(Ops_MOVDQ, .modrm = 1, .dir = 1, .sse_sel = 1));
  set(twobyte, 0x7e, DI(Ops_MOVD, .modrm = 1, .dir = 0, .store = 1, .sse_sel = 1));
  set(twobyte, 0x7f, DI(Ops_MOVDQ, .modrm = 1, .dir = 0, .store = 1, .sse_sel = 1));
  /* A0..A3: MOV AL/eAX <-> moffs.  The address is an immediate, not a ModRM
     operand, so these carry no ModRM byte at all. */
  primary[0xa0] = DI(Ops_MOV, .modrm = 0, .imm = IMM_ADDR);
  primary[0xa1] = DI(Ops_MOV, .modrm = 0, .imm = IMM_ADDR);
  primary[0xa2] = DI(Ops_MOV, .modrm = 0, .imm = IMM_ADDR);
  primary[0xa3] = DI(Ops_MOV, .modrm = 0, .imm = IMM_ADDR);
  set(twobyte, 0xa2, DI(Ops_CPUID, .modrm = 0));
  set(twobyte, 0xaf, DI(Ops_IMUL, .modrm = 1, .dir = 1));
  set(twobyte, 0xb6, DI(Ops_MOVZX, .modrm = 1, .dir = 1, .form = FORM_MOVZX8));
  set(twobyte, 0xb7, DI(Ops_MOVZX, .modrm = 1, .dir = 1, .form = FORM_MOVZX16));
  set(twobyte, 0xbe, DI(Ops_MOVSX, .modrm = 1, .dir = 1, .form = FORM_MOVSX8));
  set(twobyte, 0xbf, DI(Ops_MOVSX, .modrm = 1, .dir = 1, .form = FORM_MOVSX16));
  set(twobyte, 0xd6, DI(Ops_MOVQ, .modrm = 1, .dir = 0, .store = 1, .sse_sel = 1));
  set(twobyte, 0xc0, DI(Ops_XADD, .modrm = 1, .dir = 0, .force8 = 1));
  set(twobyte, 0xc1, DI(Ops_XADD, .modrm = 1, .dir = 0));
  set(twobyte, 0xe6, DI(Ops_CVTDQ2PD, .modrm = 1, .dir = 1));
  set(twobyte, 0xbc, DI(Ops_BSF, .modrm = 1, .dir = 1));
  set(twobyte, 0xbd, DI(Ops_BSR, .modrm = 1, .dir = 1));
  set(twobyte, 0xa3, DI(Ops_BT, .modrm = 1, .dir = 0));
  set(twobyte, 0xab, DI(Ops_BTS, .modrm = 1, .dir = 0));
  set(twobyte, 0xb3, DI(Ops_BTR, .modrm = 1, .dir = 0));
  set(twobyte, 0xbb, DI(Ops_BTC, .modrm = 1, .dir = 0));
  set(twobyte, 0xba, DI(Ops_UNDEF, .modrm = 1, .imm = IMM_8));
  set(twobyte, 0xb0, DI(Ops_CMPXCHG, .modrm = 1, .dir = 0, .force8 = 1));
  set(twobyte, 0xb1, DI(Ops_CMPXCHG, .modrm = 1, .dir = 0));
  set(twobyte, 0xc7, DI(Ops_UNDEF, .modrm = 1));
  set(twobyte, 0xfc, DI(Ops_PADDB, .modrm = 1, .dir = 1));
  set(twobyte, 0xfd, DI(Ops_PADDW, .modrm = 1, .dir = 1));
  set(twobyte, 0xd4, DI(Ops_PADDQ, .modrm = 1, .dir = 1));
  set(twobyte, 0xf8, DI(Ops_PSUBB, .modrm = 1, .dir = 1));
  set(twobyte, 0xf9, DI(Ops_PSUBW, .modrm = 1, .dir = 1));
  set(twobyte, 0xfb, DI(Ops_PSUBQ, .modrm = 1, .dir = 1));
  set(twobyte, 0x74, DI(Ops_PCMPEQB, .modrm = 1, .dir = 1));
  set(twobyte, 0x75, DI(Ops_PCMPEQW, .modrm = 1, .dir = 1));
  set(twobyte, 0x76, DI(Ops_PCMPEQD, .modrm = 1, .dir = 1));
  set(twobyte, 0x64, DI(Ops_PCMPGTB, .modrm = 1, .dir = 1));
  set(twobyte, 0x65, DI(Ops_PCMPGTW, .modrm = 1, .dir = 1));
  set(twobyte, 0x66, DI(Ops_PCMPGTD, .modrm = 1, .dir = 1));
  set(twobyte, 0x70, DI(Ops_PSHUFD, .modrm = 1, .dir = 1, .imm = IMM_8));
  set(twobyte, 0x60, DI(Ops_PUNPCKLBW, .modrm = 1, .dir = 1));
  set(twobyte, 0x61, DI(Ops_PUNPCKLWD, .modrm = 1, .dir = 1));
  set(twobyte, 0x62, DI(Ops_PUNPCKLDQ, .modrm = 1, .dir = 1));
  set(twobyte, 0x6c, DI(Ops_PUNPCKLQDQ, .modrm = 1, .dir = 1));
  set(twobyte, 0x68, DI(Ops_PUNPCKHBW, .modrm = 1, .dir = 1));
  set(twobyte, 0x69, DI(Ops_PUNPCKHWD, .modrm = 1, .dir = 1));
  set(twobyte, 0x6a, DI(Ops_PUNPCKHDQ, .modrm = 1, .dir = 1));
  set(twobyte, 0x6d, DI(Ops_PUNPCKHQDQ, .modrm = 1, .dir = 1));
  set(twobyte, 0xd7, DI(Ops_PMOVMSKB, .modrm = 1, .dir = 1));
  set(twobyte, 0xe7, DI(Ops_MOVNTDQ, .modrm = 1, .dir = 0, .store = 1));
  set(twobyte, 0xdb, DI(Ops_SIMD_PAND, .modrm = 1, .dir = 1));
  set(twobyte, 0xeb, DI(Ops_SIMD_POR, .modrm = 1, .dir = 1));
  set(twobyte, 0xef, DI(Ops_SIMD_PXOR, .modrm = 1, .dir = 1));
  set(twobyte, 0xfe, DI(Ops_SIMD_PADDD, .modrm = 1, .dir = 1));
  set(twobyte, 0xfa, DI(Ops_SIMD_PSUBD, .modrm = 1, .dir = 1));

  static const int shifts[8] = { Ops_ROL, Ops_ROR, Ops_RCL, Ops_RCR,
                                 Ops_SHL, Ops_SHR, Ops_SHL, Ops_SAR };
  for (int i = 0; i < 8; i++) {
    groups[0][i] = DI(alu[i], .dir = 0);
    groups[1][i] = DI(shifts[i], .dir = 0);
  }
  groups[2][0] = DI(Ops_TEST, .imm = IMM_Z);
  groups[2][1] = DI(Ops_TEST, .imm = IMM_Z);
  groups[2][2] = DI(Ops_NOT);
  groups[2][3] = DI(Ops_NEG);
  groups[2][4] = DI(Ops_MUL);
  groups[2][5] = DI(Ops_IMUL);
  groups[2][6] = DI(Ops_DIV);
  groups[2][7] = DI(Ops_IDIV);
  groups[3][0] = DI(Ops_INC);
  groups[3][1] = DI(Ops_DEC);
  groups[4][0] = DI(Ops_INC);
  groups[4][1] = DI(Ops_DEC);
  groups[4][2] = DI(Ops_CALL);
  groups[4][3] = DI(Ops_CALL);
  groups[4][4] = DI(Ops_JMP);
  groups[4][5] = DI(Ops_JMP);
  groups[4][6] = DI(Ops_PUSH);
  groups[5][0] = DI(Ops_MOV);
  groups[6][0] = DI(Ops_POP);
  tables_ready = 1;
}

static uint64_t read_le(const uint8_t *b, int n) {
  uint64_t v = 0;
  for (int i = 0; i < n; i++) v |= (uint64_t)b[i] << (8 * i);
  return v;
}

static int64_t sext(uint64_t v, int bits) {
  uint64_t m = (1ull << bits) - 1;
  v &= m;
  if (v & (1ull << (bits - 1))) v |= ~m;
  return (int64_t)v;
}

/* x87 opcode groups D8..DF.
 *
 * `esc` is 0..7 for D8..DF, `reg` the ModRM reg field, `mod`/`rm` the operand.
 * When mod != 3 the operand is memory and its width depends on the escape;
 * when mod == 3 the r/m field selects ST(i). */
static DInfo x87m(uint8_t op, uint8_t memsz) {
  DInfo d = {0};
  d.op = op;
  d.modrm = 1;
  d.form = memsz;               /* memory operand width */
  return d;
}

static DInfo x87r(uint8_t op, int idx) {
  DInfo d = {0};
  d.op = op;
  d.modrm = 1;
  d.form = 0;                   /* register form */
  d.st_idx = (uint8_t)(idx & 7);
  d.has_st = 1;
  return d;
}

/* x87 escape groups D8..DF.
 *
 * ModRM semantics differ sharply between the two forms:
 *
 *   memory operand (mod != 3):  the REG field selects the operation and the
 *                               r/m field is the address.
 *   register operand (mod == 3): the REG field selects the operation and the
 *                               r/m field picks ST(i) -- with a few notable
 *                               exceptions (D9/D8/DA/DE arithmetic read the
 *                               destination ST(i) from rm as well).
 *
 * The register branch used to switch on `rm` and pass `reg` as the ST index,
 * which is exactly backwards: it decoded `fld st(i)` as FXSAVE, `fxch st(i)`
 * as FXRSTOR, `fchs`/`fabs`/`ftst`/`fxam` as #UD, and silently turned the
 * whole DA..DF register space into NOPs. */
static DInfo x87_group(int esc, int reg, int mod, int rm) {
  int is_reg = (mod == 3);

  /* ---- memory forms: the REG field is the operation ------------------ */
  if (!is_reg) {
    switch (esc) {
      case 0:  /* D8: m32real */
        switch (reg) {
          case 0: return x87m(Ops_FADD, 4);
          case 1: return x87m(Ops_FMUL, 4);
          case 2: return x87m(Ops_FCOM, 4);
          case 3: return x87m(Ops_FCOMP, 4);
          case 4: return x87m(Ops_FSUB, 4);
          case 5: return x87m(Ops_FSUBR, 4);
          case 6: return x87m(Ops_FDIV, 4);
          case 7: return x87m(Ops_FDIVR, 4);
        }
        break;
      case 1:  /* D9: m32real loads/stores and the control words */
        switch (reg) {
          case 0: return x87m(Ops_FLD, 4);
          case 2: return x87m(Ops_FST, 4);
          case 3: return x87m(Ops_FSTP, 4);
          case 4: return x87m(Ops_FNSTENV, 4);
          case 5: return x87m(Ops_FLDCW, 4);
          case 6: return x87m(Ops_FNSTCW, 4);
          case 7: return x87m(Ops_FNSTSW, 4);
        }
        break;
      case 2:  /* DA: m32int */
        switch (reg) {
          case 0: return x87m(Ops_FILD, 4);
          case 1: return x87m(Ops_FISTTP, 4);
          case 2: return x87m(Ops_FIST, 4);
          case 3: return x87m(Ops_FISTP, 4);
          case 5: return x87m(Ops_FLD, 10);      /* FLD m80real */
          case 7: return x87m(Ops_FSTP, 10);
        }
        break;
      case 3:  /* DB: m32int / m80real */
        switch (reg) {
          case 0: return x87m(Ops_FILD, 4);
          case 1: return x87m(Ops_FISTTP, 4);
          case 2: return x87m(Ops_FIST, 4);
          case 3: return x87m(Ops_FISTP, 4);
          case 5: return x87m(Ops_FLD, 10);
          case 7: return x87m(Ops_FSTP, 10);
        }
        break;
      case 4:  /* DC: m64real */
        switch (reg) {
          case 0: return x87m(Ops_FADD, 8);
          case 1: return x87m(Ops_FMUL, 8);
          case 2: return x87m(Ops_FCOM, 8);
          case 3: return x87m(Ops_FCOMP, 8);
          case 4: return x87m(Ops_FSUB, 8);
          case 5: return x87m(Ops_FSUBR, 8);
          case 6: return x87m(Ops_FDIV, 8);
          case 7: return x87m(Ops_FDIVR, 8);
        }
        break;
      case 5:  /* DD: m64real / m64int */
        switch (reg) {
          case 0: return x87m(Ops_FLD, 8);
          case 1: return x87m(Ops_FISTTP, 8);
          case 2: return x87m(Ops_FST, 8);
          case 3: return x87m(Ops_FSTP, 8);
          case 4: return x87m(Ops_FNSAVE, 8);
          case 6: return x87m(Ops_FNSTSW, 8);
          case 7: return x87m(Ops_FNSTSW, 8);
        }
        break;
      case 6:  /* DE: m16int */
        switch (reg) {
          case 0: return x87m(Ops_FADD, 2);
          case 1: return x87m(Ops_FMUL, 2);
          case 2: return x87m(Ops_FCOM, 2);
          case 3: return x87m(Ops_FCOMP, 2);
          case 4: return x87m(Ops_FSUB, 2);
          case 5: return x87m(Ops_FSUBR, 2);
          case 6: return x87m(Ops_FDIV, 2);
          case 7: return x87m(Ops_FDIVR, 2);
        }
        break;
      case 7:  /* DF: m16int / m64int */
        switch (reg) {
          case 0: return x87m(Ops_FILD, 2);
          case 1: return x87m(Ops_FISTTP, 2);
          case 2: return x87m(Ops_FIST, 2);
          case 3: return x87m(Ops_FISTP, 2);
          case 5: return x87m(Ops_FILD, 8);
          case 7: return x87m(Ops_FISTP, 8);
        }
        break;
      default: break;
    }
    return (DInfo){0};
  }

  /* ---- register forms: REG is the operation, rm is ST(i) ------------- */
  switch (esc) {
    case 0:  /* D8 C0..DF: FADD/FMUL/FCOM/FCOMP/FSUB/FSUBR/FDIV/FDIVR ST(0),ST(i) */
      switch (reg) {
        case 0: return x87r(Ops_FADD, rm);
        case 1: return x87r(Ops_FMUL, rm);
        case 2: return x87r(Ops_FCOM, rm);
        case 3: return x87r(Ops_FCOMP, rm);
        case 4: return x87r(Ops_FSUB, rm);
        case 5: return x87r(Ops_FSUBR, rm);
        case 6: return x87r(Ops_FDIV, rm);
        case 7: return x87r(Ops_FDIVR, rm);
      }
      break;
    case 1:  /* D9 C0..CF: FLD/FXCH, D9 D0: FNOP, D9 E0..FF: constants */
      switch (reg) {
        case 0: return x87r(Ops_FLD, rm);
        case 1: return x87r(Ops_FXCH, rm);
        case 2:
          /* D9 D0 is FNOP; D9 D8 is FSTP ST(i) (documented as a real pop). */
          return (rm == 0) ? x87m(Ops_FNOP, 0) : x87r(Ops_FSTP, rm);
        case 3:
          /* FSTP ST(i): pops either way, so treat both halves as the pop. */
          return x87r(Ops_FSTP, rm);
        default: break;
      }
      /* D9 E0..FF: the constant/transcendental block.  REG 4 and 5 hold the
         no-operand group split by rm; REG 6 and 7 hold the transcendentals. */
      if (reg == 4) {
        switch (rm) {
          case 0: return x87m(Ops_FCHS, 0);
          case 1: return x87m(Ops_FABS, 0);
          case 4: return x87m(Ops_FTST, 0);
          case 5: return x87m(Ops_FXAM, 0);
          default: break;
        }
      }
      if (reg == 5) {
        static const uint16_t k[8] = {Ops_FLD1, Ops_FLDL2T, Ops_FLDL2E,
                                     Ops_FLDPI, Ops_FLDLG2, Ops_FLDLN2,
                                     Ops_FLDZ, Ops_UNDEF};
        if (k[rm] != Ops_UNDEF) return x87m(k[rm], 0);
      }
      if (reg == 6) {
        static const uint16_t t[8] = {Ops_F2XM1, Ops_FYL2X, Ops_FPTAN,
                                     Ops_FPATAN, Ops_FXTRACT, Ops_FPREM1,
                                     Ops_FDECSTP, Ops_FINCSTP};
        return x87m(t[rm], 0);
      }
      if (reg == 7) {
        static const uint16_t t[8] = {Ops_FPREM, Ops_FYL2XP1, Ops_FSQRT,
                                     Ops_FSINCOS, Ops_FRNDINT, Ops_FSCALE,
                                     Ops_FSIN, Ops_FCOS};
        return x87m(t[rm], 0);
      }
      break;
    case 2:  /* DA C0..DF: FCMOVcc, DA E9: FUCOMPP */
      if (reg == 5 && rm == 1) return x87m(Ops_FCOMPP, 0);    /* DA E9 FUCOMPP */
      switch (reg) {
        case 0: return x87r(Ops_FCMOVB, rm);
        case 1: return x87r(Ops_FCMOVE, rm);
        case 2: return x87r(Ops_FCMOVBE, rm);
        case 3: return x87r(Ops_FCMOVU, rm);
        default: break;
      }
      break;
    case 3:  /* DB C0..DF: FCMOVcc (complemented), DB E0..FF: misc */
      switch (reg) {
        case 0: return x87r(Ops_FCMOVNB, rm);
        case 1: return x87r(Ops_FCMOVNE, rm);
        case 2: return x87r(Ops_FCMOVNBE, rm);
        case 3: return x87r(Ops_FCMOVNU, rm);
        case 4:
          if (rm == 0) return x87m(Ops_FNOP, 0);   /* FENI/FDISI: obsolete */
          if (rm == 1) return x87m(Ops_FNOP, 0);   /* FDISI */
          if (rm == 2) return x87m(Ops_FNCLEX, 0);
          if (rm == 3) return x87m(Ops_FNINIT, 0);
          return x87m(Ops_FNOP, 0);
        case 5: return x87r(Ops_FUCOMI, rm);
        case 6: return x87r(Ops_FCOMI, rm);
        default: break;
      }
      break;
    case 4:  /* DC C0..DF: FADD/FMUL/FCOM/FCOMP/FSUBR/FSUB/FDIVR/FDIV ST(i),ST(0) */
      switch (reg) {
        case 0: return x87r(Ops_FADD, rm);
        case 1: return x87r(Ops_FMUL, rm);
        case 2: return x87r(Ops_FCOM, rm);
        case 3: return x87r(Ops_FCOMP, rm);
        case 4: return x87r(Ops_FSUBR, rm);
        case 5: return x87r(Ops_FSUB, rm);
        case 6: return x87r(Ops_FDIVR, rm);
        case 7: return x87r(Ops_FDIV, rm);
      }
      break;
    case 5:  /* DD C0..DF: FFREE/FXCH/FST/FSTP/FUCOM/FUCOMP, DD E0..: misc */
      switch (reg) {
        case 0: return x87r(Ops_FFREE, rm);
        case 1: return x87r(Ops_FXCH, rm);
        case 2: return x87r(Ops_FST, rm);
        case 3: return x87r(Ops_FSTP, rm);
        case 4: return x87r(Ops_FUCOM, rm);
        case 5: return x87r(Ops_FUCOMP, rm);
        default: break;
      }
      break;
    case 6:  /* DE C0..DF: FADDP/FMULP/FCOMPP/FSUBRP/FSUBP/FDIVRP/FDIVP */
      switch (reg) {
        case 0: return x87r(Ops_FADDP, rm);
        case 1: return x87r(Ops_FMULP, rm);
        /* DE D0..D7 is an undocumented FCOMP ST(i) (Bochs models it too). */
        case 2: return x87r(Ops_FCOMP, rm);
        /* DE D9 is FCOMPP (reg 3, rm 1); DE D8 and DA..DF are reserved.
           Note the reg/rm split: FCOMPP is NOT in the /2 group. */
        case 3: return (rm == 1) ? x87m(Ops_FCOMPP, 0) : (DInfo){0};
        case 4: return x87r(Ops_FSUBRP, rm);
        case 5: return x87r(Ops_FSUBP, rm);
        case 6: return x87r(Ops_FDIVRP, rm);
        case 7: return x87r(Ops_FDIVP, rm);
      }
      break;
    case 7:  /* DF C0..DF: FFREE/FSTP, DF E0: FNSTSW AX, DF E8..: FCOMIP */
      switch (reg) {
        case 0: return x87r(Ops_FFREE, rm);
        case 2: return x87r(Ops_FSTP, rm);
        case 3: return x87r(Ops_FSTP, rm);
        /* DF E0 = FNSTSW AX (the only /4 encoding); the rest of /4 is unused. */
        case 4:
          if (rm == 0) { DInfo d = x87m(Ops_FNSTSW, 0); return d; }
          break;
        case 5: return x87r(Ops_FUCOMIP, rm);
        case 6: return x87r(Ops_FCOMIP, rm);
        default: break;
      }
      break;
    default: break;
  }
  return (DInfo){0};
}


/* ------------------------------------------------------------ decode cache
 *
 * A profile of a Linux boot puts ~69% of the run time inside xemu_decode():
 * the kernel re-executes the same few thousand instructions over and over
 * while the decode work (prefix scan, table lookup, ModRM/SIB) is identical
 * every time.  A direct-mapped cache keyed by PC turns that into a compare
 * plus a struct copy.
 *
 * Correctness rests on xemu_code_gen (mem.c): every store into a page that
 * has ever held decoded code bumps it, as does a CR3 load or INVLPG (both can
 * remap a PC to different bytes).  A cache hit therefore also proves that the
 * instruction bytes behind this PC have not changed since it was decoded.
 *
 * The decode tests and the ISA diff write their vectors through
 * xemu_mem_write(), so they invalidate the cache exactly like guest code. */
#define DEC_CACHE_SIZE 4096
typedef struct {
  uint64_t rip;
  uint64_t gen;
  XemuInst inst;
} DecCacheEnt;
static DecCacheEnt dec_cache[DEC_CACHE_SIZE];

int xemu_decode(uint64_t rip, XemuInst *out) {
  init_tables();
  DecCacheEnt *ce = &dec_cache[(rip >> 1) & (DEC_CACHE_SIZE - 1)];
  if (ce->gen == xemu_code_gen && ce->rip == rip) {
    *out = ce->inst;
    return 0;
  }
  uint8_t buf[16];
  uint64_t pa, ferr;
  if (xemu_xlate_fetch(rip, &pa, &ferr) != 0) {
    xemu_raise_exc(14, ferr, rip);
    return 1; /* fetched exception */
  }
  if (xemu_vread(rip, buf, 16) != 0) return -1;
  memset(out, 0, sizeof(*out));

  int off = 0, p66 = 0, p67 = 0, rep = 0, lock = 0, rex = 0, rex_valid = 0;
  int seg_ovr = 0;          /* 0 none, 1 ES, 2 CS, 3 SS, 4 DS, 5 FS, 6 GS */
  int overflow = 0;
  /* Up to five prefix slots, mirroring the RTL prefix scanner. */
  for (int n = 0; n < 5; n++) {
    uint8_t b = buf[off];
    int is_rex = (b >= 0x40 && b <= 0x4f);
    int is_legacy = (b == 0x66 || b == 0x67 || b == 0xf0 || b == 0xf2 || b == 0xf3 ||
                     b == 0x2e || b == 0x36 || b == 0x3e || b == 0x26 || b == 0x64 || b == 0x65);
    if (!is_rex && !is_legacy) break;
    if (off == 14) { overflow = 1; break; }
    off++;
    if (b == 0x66) p66 = 1;
    else if (b == 0x67) p67 = 1;
    else if (b == 0xf3) rep = 1;
    else if (b == 0xf2) rep = 2;
    else if (b == 0xf0) lock = 1;
    else if (b == 0x26) seg_ovr = 1;
    else if (b == 0x2e) seg_ovr = 2;
    else if (b == 0x36) seg_ovr = 3;
    else if (b == 0x3e) seg_ovr = 4;
    else if (b == 0x64) seg_ovr = 5;
    else if (b == 0x65) seg_ovr = 6;
    if (is_rex) { rex = b; rex_valid = 1; }
    else if (is_legacy) { rex = 0; rex_valid = 0; }
  }
  (void)rex_valid;

  int opOff = off;
  uint8_t b0 = buf[opOff];
  int is0F = (b0 == 0x0f);
  uint8_t op1 = buf[opOff + 1];
  int is3 = is0F && (op1 == 0x38 || op1 == 0x3a);
  uint8_t opcode = is0F ? op1 : b0;
  int opcodeLen = is0F ? 2 : 1;
  int afterOpcode = opOff + opcodeLen;

  DInfo base = is0F ? twobyte[opcode] : primary[opcode];
  uint8_t modrm = buf[afterOpcode];
  int mod = modrm >> 6, reg = (modrm >> 3) & 7, rm = modrm & 7;
  int rm_is_reg = (mod == 3);
  int has_modrm = base.modrm && !is3;

  DInfo gi = {0};
  /* D8..DF occupy the eight group slots G_X87..G_X87+7.  The bound must be
     +8 (not G_15, which is a separate group for 0F AE). */
  int group_is_x87 = (base.group >= G_X87 && base.group <= G_X87 + 7);
  int g_active = has_modrm && base.group != 0;
  if (g_active) {
    /* 0F AE (G_15) is a system group, not an x87 escape: the >= G_X87 test
       used to swallow it and silently decode FXSAVE/FXRSTOR/fences/MXCSR as
       garbage x87 operations. */
    if (group_is_x87) gi = x87_group(base.group - G_X87, reg, mod, rm);
    else gi = groups[base.group - 1][reg];
  }

  int imm_kind = (g_active && gi.imm != 0) ? gi.imm : base.imm;
  int dir = g_active ? gi.dir : base.dir;
  int form = g_active ? gi.form : base.form;
  int op = is3 ? Ops_UNDEF : (g_active ? gi.op : base.op);
  int is_x87 = group_is_x87;
  int force8 = base.force8;
  int sse_sel = base.sse_sel && !g_active;

  int isE6 = is0F && opcode == 0xe6 && !is3;
  int is5A = is0F && opcode == 0x5a && !is3;
  int is2A = is0F && opcode == 0x2a && !is3;
  int is2C = is0F && opcode == 0x2c && !is3;
  int is2D = is0F && opcode == 0x2d && !is3;
  int is5B = is0F && opcode == 0x5b && !is3;
  int isLidt = is0F && opcode == 0x01 && !is3 && reg == 3;
  int isGdtLtr = is0F && opcode == 0x01 && !is3;
  int isInvlpg = is0F && opcode == 0x01 && !is3 && reg == 7;
  if (isE6) op = p66 ? Ops_CVTTPD2DQ : (rep == 1 ? Ops_CVTDQ2PD : (rep == 2 ? Ops_CVTPD2DQ : Ops_UNDEF));
  else if (is5A) op = rep == 1 ? Ops_CVTSS2SD : (rep == 2 ? Ops_CVTSD2SS : Ops_UNDEF);
  else if (is2A) op = rep == 1 ? Ops_CVTSI2SS : (rep == 2 ? Ops_CVTSI2SD : Ops_UNDEF);
  else if (is2C) op = rep == 1 ? Ops_CVTTSS2SI : (rep == 2 ? Ops_CVTTSD2SI : Ops_UNDEF);
  else if (is2D) op = rep == 2 ? Ops_CVTSD2SI : Ops_UNDEF;
  else if (is5B) op = p66 ? Ops_CVTPS2DQ : Ops_CVTDQ2PS;
  else if (isLidt) op = Ops_LIDT;
  else if (isInvlpg) op = Ops_INVLPG;
  /* 0F 01: SGDT/SIDT/LGDT/LIDT/SMSW/LMSW/INVLPG (the /4 and /5 slots are
     VERR/VERW for 0F 00 and unused here). */
  if (is0F && opcode == 0x01 && !is3) {
    /* SMSW/LMSW accept a register or memory operand; SGDT/SIDT/LGDT/LIDT are
       memory-only. */
    if (reg == 4) op = Ops_SMSW;
    else if (reg == 6) op = Ops_LMSW;
    else if (!rm_is_reg) {
      switch (reg) {
        case 0: op = Ops_SGDT; break;
        case 1: op = Ops_SIDT; break;
        case 2: op = Ops_LGDT; break;
        case 3: op = Ops_LIDT; break;
        default: break;
      }
    }
  }
  /* 0F 00: SLDT/STR/LLDT/LTR/VERR/VERW. */
  if (is0F && opcode == 0x00) {
    if (reg == 0) op = Ops_SLDT;
    else if (reg == 1) op = Ops_STR;
    else if (reg == 2) op = Ops_LLDT;
    else if (reg == 3) op = Ops_LTR;
    else if (reg == 4) op = Ops_VERR;
    else if (reg == 5) op = Ops_VERW;
  }
  /* 0F 01 with a register ModRM carries a whole family of system ops. */
  if (is0F && opcode == 0x01 && rm_is_reg) {
    switch (modrm) {
      case 0xc8: op = Ops_MONITOR; break;
      case 0xc9: op = Ops_MWAIT; break;
      case 0xd0: op = Ops_XGETBV; break;
      case 0xd1: op = Ops_XSETBV; break;
      case 0xe8: op = Ops_SERIALIZE; break;
      case 0xca: op = Ops_CLAC; break;
      case 0xcb: op = Ops_STAC; break;
      case 0xf8: op = Ops_SWAPGS; break;
      case 0xf9: op = Ops_RDTSCP; break;
      default: break;
    }
  }
  /* F3 0F B8 POPCNT, 0F C7 /6 RDRAND, /7 RDSEED. */
  /* 0F 18 PREFETCHT0/T1/T2/NTA and 0F 0D PREFETCHW are pure hints. */
  if (is0F && (opcode == 0x18 || opcode == 0x0d)) op = Ops_PREFETCH;
  if (is0F && opcode == 0xc7) {
    if (reg == 6) op = Ops_RDRAND;
    else if (reg == 7) op = Ops_RDSEED;
    else if (reg == 3 && rep == 1) op = Ops_XRSTORS;
    else if (reg == 4 && rep == 1) op = Ops_XSAVEC;
    else if (reg == 5 && rep == 1) op = Ops_XSAVES;
  }
  /* F3 0F AE /0-/3: RDFSBASE/RDGSBASE/WRFSBASE/WRGSBASE. */
  if (is0F && opcode == 0xae && rm_is_reg && rep == 1) {
    switch (reg) {
      case 0: op = Ops_RDFSBASE; break;
      case 1: op = Ops_RDGSBASE; break;
      case 2: op = Ops_WRFSBASE; break;
      case 3: op = Ops_WRGSBASE; break;
      default: break;
    }
  }
  /* F2 0F 00 /6: LKGS (LAM-related; accept as a no-op). */
  /* F2 0F 00 /6 LKGS, with either a register or a memory operand. */
  if (is0F && opcode == 0x00 && rep == 2 && reg == 6) op = Ops_LKGS;
  /* F3 90 is PAUSE (the REP prefix plus NOP). */
  if (is0F == 0 && opcode == 0x90 && rep == 1) op = Ops_PAUSE;
  if (is0F && opcode == 0xba) {
    static const uint16_t btops[8] = {0,0,0,0,Ops_BT,Ops_BTS,Ops_BTR,Ops_BTC};
    op = btops[reg];
    imm_kind = IMM_8;
    dir = 0;
  }
  if (is0F && opcode == 0xc7 && reg == 1) {
    op = Ops_CMPXCHG8B;
    dir = 0;
  }
  /* 0F C2 CMPPS/CMPPD/CMPSS/CMPSD: the immediate byte selects the predicate. */
  if (is0F && opcode == 0xc2 && !is3) op = Ops_SIMD_CMP;
  /* 66 0F 71/72/73 shift-by-immediate.  The opcode byte picks the element
     width (word/dword/qword) and the ModRM reg field picks the operation;
     both are packed into `cond` for the executor. */
  if (is0F && p66 && (opcode == 0x71 || opcode == 0x72 || opcode == 0x73)) {
    op = Ops_PSHIFT_IMM;
    imm_kind = IMM_8;
  }
  if (is0F && opcode == 0x70) {
    if (p66) op = Ops_PSHUFD;
    else if (rep == 1) op = Ops_PSHUFHW;
    else if (rep == 2) op = Ops_PSHUFLW;
  }

  int rexW = (rex >> 3) & 1, rexR = (rex >> 2) & 1, rexX = (rex >> 1) & 1, rexB = rex & 1;
  int has_sib = has_modrm && !rm_is_reg && rm == 4;
  uint8_t sib = buf[afterOpcode + 1];
  int sib_base = sib & 7, sib_idx = (sib >> 3) & 7, sib_scale = sib >> 6;
  int disp_off = afterOpcode + (has_modrm ? 1 : 0) + (has_sib ? 1 : 0);
  int disp_len = 0;
  if (has_modrm && !rm_is_reg) {
    if (mod == 1) disp_len = 1;
    else if (mod == 2) disp_len = 4;
    else if (rm == 5 && !has_sib) disp_len = 4;
    else if (has_sib && sib_base == 5) disp_len = 4;
  }
  int64_t disp = 0;
  if (disp_len == 1) disp = sext(buf[disp_off], 8);
  else if (disp_len == 4) disp = sext(read_le(&buf[disp_off], 4), 32);

  int opsz_bytes = force8 ? 1 : (rexW ? 8 : (p66 ? 2 : 4));
  int opsz = opsz_bytes;
  int eff_imm = (force8 && imm_kind == IMM_Z) ? IMM_8 : imm_kind;
  int imm_len = 0;
  switch (eff_imm) {
    case IMM_8: imm_len = 1; break;
    case IMM_Z: imm_len = (opsz == 2) ? 2 : 4; break;
    case IMM_32: imm_len = 4; break;
    case IMM_64: imm_len = (opsz == 2) ? 2 : (rexW ? 8 : 4); break;
    case IMM_16: imm_len = 2; break;
    case IMM_ADDR: imm_len = p67 ? 4 : 8; break;
    default: imm_len = 0; break;
  }
  int imm_off = disp_off + disp_len;
  uint64_t imm = 0;
  switch (eff_imm) {
    case IMM_8: imm = (uint64_t)sext(buf[imm_off], 8); break;
    case IMM_16: imm = read_le(&buf[imm_off], 2); break;
    case IMM_32: imm = (uint64_t)sext(read_le(&buf[imm_off], 4), 32); break;
    case IMM_Z: imm = (opsz == 2) ? (uint64_t)sext(read_le(&buf[imm_off], 2), 16)
                                  : (uint64_t)sext(read_le(&buf[imm_off], 4), 32); break;
    case IMM_64: imm = (opsz == 2) ? read_le(&buf[imm_off], 2)
                                  : (rexW ? read_le(&buf[imm_off], 8)
                                          : read_le(&buf[imm_off], 4)); break;
    case IMM_ADDR: imm = p67 ? read_le(&buf[imm_off], 4)
                             : read_le(&buf[imm_off], 8); break;
    default: imm = 0; break;
  }
  int inst_len = imm_off + imm_len;

  out->len = inst_len;
  out->op = op;
  out->dir = (uint8_t)dir;
  out->cond = opcode & 0xf;
  out->opsz = opsz;
  out->imm = imm;
  out->imm_kind = (uint8_t)eff_imm;
  out->rep = (uint8_t)rep;
  out->disp = disp;
  out->illegal = 0;
  out->a_kind = XK_NONE; out->a_idx = 0;
  out->b_kind = XK_NONE; out->b_idx = 0;
  out->base = 0; out->index = 0; out->scale = 0; out->rip_rel = 0;

  int rm_kind = rm_is_reg ? XK_GPR : XK_MEM;
  int a_idx = dir == 1 ? (reg | (rexR << 3)) : (rm | (rexB << 3));
  int b_idx = dir == 1 ? (rm | (rexB << 3)) : (reg | (rexR << 3));
  out->a_kind = dir == 1 ? XK_GPR : rm_kind;
  out->a_idx = a_idx;
  out->b_kind = dir == 1 ? rm_kind : XK_GPR;
  out->b_idx = b_idx;

  out->base = has_sib ? (sib_base | (rexB << 3)) : (rm | (rexB << 3));
  out->index = sib_idx | (rexX << 3);
  out->scale = sib_scale;
  out->has_base = has_modrm && !rm_is_reg && !(mod == 0 && rm == 5 && !has_sib) &&
                  !(has_sib && mod == 0 && sib_base == 5);
  out->has_index = has_sib && !(sib_idx == 4 && !rexX);
  /* Keep the raw rm/sib-base id (the RTL exposes mem.base even when hasBase
   * is false; field-level difftest compares it). */
  out->rip_rel = has_modrm && !rm_is_reg && !has_sib && mod == 0 && rm == 5;

  int is_xmm = (op == Ops_MOVD || op == Ops_MOVQ || op == Ops_MOVDQ || op == Ops_MOVPS ||
                (op >= Ops_SIMD_ADD && op <= Ops_CVTTPD2DQ) || op == Ops_SIMD_PAND ||
                op == Ops_SIMD_POR || op == Ops_SIMD_PXOR || op == Ops_SIMD_PADDD || op == Ops_SIMD_PSUBD ||
                (op >= Ops_PADDB && op <= Ops_MOVNTDQ) || op == Ops_PSHUFHW || op == Ops_PSHUFLW);
  int simd = SIMD_NONE;
  int mand = rep == 1 ? 2 : (rep == 2 ? 3 : (p66 ? 1 : 0));
  static const int simd_arith[4] = { SIMD_PS, SIMD_PD, SIMD_SS, SIMD_SD };
  switch (op) {
    case Ops_MOVPS: case Ops_SIMD_ADD: case Ops_SIMD_SUB: case Ops_SIMD_MUL:
    case Ops_SIMD_DIV: case Ops_SIMD_SQRT: case Ops_SIMD_MIN: case Ops_SIMD_MAX:
      simd = simd_arith[mand]; break;
    case Ops_SIMD_UCOMI: case Ops_SIMD_COMI: simd = p66 ? SIMD_SD : SIMD_SS; break;
    case Ops_MOVDQ: simd = SIMD_DQ; break;
    case Ops_MOVD: simd = rexW ? SIMD_SD : SIMD_SS; break;
    case Ops_MOVQ: simd = SIMD_SD; break;
    case Ops_CVTSI2SD: case Ops_CVTTSD2SI: case Ops_CVTSD2SI: case Ops_CVTSD2SS: simd = SIMD_SD; break;
    case Ops_CVTSI2SS: case Ops_CVTTSS2SI: case Ops_CVTSS2SD: simd = SIMD_SS; break;
    default: break;
  }
  if (op == Ops_MOVD) opsz = rexW ? 8 : 4;
  else if (op == Ops_MOVQ) opsz = 8;
  else if (op == Ops_CVTTSD2SI || op == Ops_CVTTSS2SI || op == Ops_CVTSD2SI ||
           op == Ops_CVTSI2SS || op == Ops_CVTSI2SD) opsz = rexW ? 8 : 4;
  else if (is_xmm) opsz = 16;
  out->opsz = opsz;
  out->simd = simd;

  int memsz = opsz_bytes;
  switch (op) {
    case Ops_MOVPS: memsz = simd == SIMD_SS ? 4 : (simd == SIMD_SD ? 8 : 16); break;
    case Ops_MOVDQ: memsz = 16; break;
    case Ops_MOVD: memsz = rexW ? 8 : 4; break;
    case Ops_MOVQ: memsz = 8; break;
    case Ops_SIMD_ADD: case Ops_SIMD_SUB: case Ops_SIMD_MUL: case Ops_SIMD_DIV:
    case Ops_SIMD_SQRT: case Ops_SIMD_MIN: case Ops_SIMD_MAX:
    case Ops_SIMD_UCOMI: case Ops_SIMD_COMI: memsz = simd == SIMD_SS ? 4 : (simd == SIMD_SD ? 8 : 16); break;
    case Ops_SIMD_PAND: case Ops_SIMD_POR: case Ops_SIMD_PXOR:
    case Ops_SIMD_PADDD: case Ops_SIMD_PSUBD: memsz = 16; break;
    case Ops_PADDB: case Ops_PADDW: case Ops_PADDQ:
    case Ops_PSUBB: case Ops_PSUBW: case Ops_PSUBQ:
    case Ops_PCMPEQB: case Ops_PCMPEQW: case Ops_PCMPEQD:
    case Ops_PCMPGTB: case Ops_PCMPGTW: case Ops_PCMPGTD:
    case Ops_PSHUFD: case Ops_PSHUFHW: case Ops_PSHUFLW:
    case Ops_PUNPCKLBW: case Ops_PUNPCKLWD: case Ops_PUNPCKLDQ: case Ops_PUNPCKLQDQ:
    case Ops_PUNPCKHBW: case Ops_PUNPCKHWD: case Ops_PUNPCKHDQ: case Ops_PUNPCKHQDQ:
    case Ops_MOVNTDQ: memsz = 16; break;
    case Ops_CVTSI2SD: case Ops_CVTSI2SS: memsz = opsz; break;
    case Ops_CVTTSS2SI: memsz = 4; break;
    case Ops_CVTSD2SI: memsz = 8; break;
    case Ops_CVTSS2SD: memsz = 4; break;
    case Ops_CVTSD2SS: memsz = 8; break;
    case Ops_MOVZX: memsz = form == FORM_MOVZX8 ? 1 : 2; break;
    case Ops_MOVSX: memsz = form == FORM_MOVSX8 ? 1 : 2; break;
    case Ops_MOVSXD: memsz = 4; break;
    case Ops_SETCC: memsz = 1; break;
    case Ops_LEA: case Ops_CALL: case Ops_JMP: case Ops_PUSH: case Ops_POP: memsz = 8; break;
    default: break;
  }
  out->memsz = memsz;

  /* Operand overrides, in the same order as Decoder.scala. */
  int opLow3 = opcode & 7;
  int acc_form = form == FORM_ACC;
  int special = form == FORM_SPECIAL;
  if (acc_form) { out->a_kind = XK_GPR; out->a_idx = 0; out->b_kind = XK_IMM; }
  if (special && op != Ops_XCHG) {
    out->a_kind = XK_GPR; out->a_idx = opLow3 | (rexB << 3);
    out->b_kind = imm_kind != 0 ? XK_IMM : XK_NONE;
  }
  if (special && op == Ops_XCHG) {
    out->a_kind = XK_GPR; out->a_idx = 0;
    out->b_kind = XK_GPR; out->b_idx = opLow3 | (rexB << 3);
  }
  if (g_active && base.group == G_1) {
    out->a_kind = rm_is_reg ? XK_GPR : XK_MEM; out->a_idx = rm | (rexB << 3);
    out->b_kind = XK_IMM;
  }
  if (g_active && base.group == G_11) {
    out->a_kind = rm_is_reg ? XK_GPR : XK_MEM; out->a_idx = rm | (rexB << 3);
    out->b_kind = XK_IMM;
  }
  if (g_active && base.group == G_3) {
    out->a_kind = rm_is_reg ? XK_GPR : XK_MEM; out->a_idx = rm | (rexB << 3);
    out->b_kind = (reg == 0 || reg == 1) ? XK_IMM : XK_NONE;
  }
  if (g_active && (base.group == G_4 || base.group == G_5)) {
    out->a_kind = rm_is_reg ? XK_GPR : XK_MEM; out->a_idx = rm | (rexB << 3);
    out->b_kind = XK_NONE;
  }
  if (g_active && base.group == G_1A) {
    out->a_kind = rm_is_reg ? XK_GPR : XK_MEM; out->a_idx = rm | (rexB << 3);
    out->b_kind = XK_NONE;
  }
  if (g_active && base.group == G_2) {
    out->a_kind = rm_is_reg ? XK_GPR : XK_MEM; out->a_idx = rm | (rexB << 3);
    if (opcode == 0xd2 || opcode == 0xd3) { out->b_kind = XK_GPR; out->b_idx = 1; }
    else if (opcode == 0xd0 || opcode == 0xd1) { out->b_kind = XK_IMM; out->imm = 1; }
    else out->b_kind = XK_IMM;
  }
  if (op == Ops_SETCC) {
    out->a_kind = rm_is_reg ? XK_GPR : XK_MEM; out->a_idx = rm | (rexB << 3);
    out->b_kind = XK_NONE;
  }
  if (op == Ops_CMOVCC) {
    out->a_kind = XK_GPR; out->a_idx = reg | (rexR << 3);
    out->b_kind = rm_is_reg ? XK_GPR : XK_MEM; out->b_idx = rm | (rexB << 3);
  }
  if (op == Ops_MOVZX || op == Ops_MOVSX || op == Ops_MOVSXD) {
    out->a_kind = XK_GPR; out->a_idx = reg | (rexR << 3);
    out->b_kind = rm_is_reg ? XK_GPR : XK_MEM; out->b_idx = rm | (rexB << 3);
  }
  if (op == Ops_LEA) {
    out->b_kind = XK_MEM; out->a_kind = XK_GPR; out->a_idx = reg | (rexR << 3);
  }
  if (op == Ops_JCC || op == Ops_LOOP) { out->a_kind = XK_NONE; out->b_kind = XK_IMM; }
  if (!g_active && (op == Ops_JMP || op == Ops_CALL)) { out->a_kind = XK_NONE; out->b_kind = XK_IMM; }
  if (op == Ops_PUSH && imm_kind != 0) { out->a_kind = XK_NONE; out->b_kind = XK_IMM; }
  if (op == Ops_RET) { out->a_kind = XK_NONE; out->b_kind = imm_kind ? XK_IMM : XK_NONE; }
  if (op == Ops_LAHF) { out->a_kind = XK_GPR; out->a_idx = 0; out->b_kind = XK_NONE; opsz = 2; out->opsz = opsz; }
  if (op == Ops_SAHF) { out->a_kind = XK_GPR; out->a_idx = 0; out->b_kind = XK_NONE; }
  if (op == Ops_LOOP) { out->a_kind = XK_GPR; out->a_idx = 1; out->b_kind = XK_NONE; out->opsz = 8; }
  if (op == Ops_PUSH || op == Ops_POP) out->opsz = p66 ? 2 : 8;
  if (op == Ops_LOOPNE || op == Ops_LOOPE || op == Ops_JRCXZ) {
    out->a_kind = XK_GPR; out->a_idx = 1; out->b_kind = XK_NONE; out->opsz = 8;
  }
  if (op == Ops_INT) { out->a_kind = XK_NONE; out->b_kind = XK_NONE; }
  if (op == Ops_NOP || op == Ops_ENDBR) { out->a_kind = XK_NONE; out->b_kind = XK_NONE; out->memsz = 0; }
  if (op == Ops_HLT || op == Ops_SYSCALL || op == Ops_CPUID || op == Ops_UD2 ||
      op == Ops_INT3 || op == Ops_INT || op == Ops_PUSHF || op == Ops_POPF ||
      op == Ops_CLC || op == Ops_STC || op == Ops_CMC || op == Ops_CLD ||
      op == Ops_STD || op == Ops_CLI || op == Ops_STI || op == Ops_CBW || op == Ops_CDQ) {
    out->a_kind = XK_NONE; out->b_kind = XK_NONE;
  }
  if (op == Ops_MOVS || op == Ops_STOS || op == Ops_LODS || op == Ops_SCAS || op == Ops_CMPS) {
    out->a_kind = XK_NONE; out->b_kind = XK_NONE;
  }
  if (is_xmm && dir == 1 && op != Ops_CVTTSD2SI && op != Ops_CVTTSS2SI &&
      op != Ops_CVTSD2SI && op != Ops_PMOVMSKB) {
    out->a_kind = XK_XMM; out->a_idx = reg | (rexR << 3);
    out->b_kind = rm_is_reg ? XK_XMM : XK_MEM; out->b_idx = rm | (rexB << 3);
    if (op == Ops_CVTSI2SD || op == Ops_CVTSI2SS)
      out->b_kind = rm_is_reg ? XK_GPR : XK_MEM;
  }
  if (is_xmm && dir == 0) {
    out->a_kind = rm_is_reg ? XK_XMM : XK_MEM; out->a_idx = rm | (rexB << 3);
    out->b_kind = XK_XMM; out->b_idx = reg | (rexR << 3);
  }
  if (op == Ops_MOVD && dir == 0) out->a_kind = rm_is_reg ? XK_GPR : XK_MEM;
  if (op == Ops_MOVD && dir == 1) out->b_kind = rm_is_reg ? XK_GPR : XK_MEM;
  if (op == Ops_CVTTSD2SI || op == Ops_CVTTSS2SI || op == Ops_CVTSD2SI) {
    out->a_kind = XK_GPR; out->a_idx = reg | (rexR << 3);
    out->b_kind = rm_is_reg ? XK_XMM : XK_MEM; out->b_idx = rm | (rexB << 3);
  }
  if (op == Ops_IN || op == Ops_OUT || op == Ops_INS || op == Ops_OUTS) {
    /* Width from the opcode's low bit plus any 0x66 prefix:
       E4/E6/6C/6E = byte, E5/E7/6D/6F = (d/q)word. */
    int wide = (opcode & 1) != 0;
    out->io_sz = (uint8_t)(wide ? (p66 ? 2 : 4) : 1);
    if (op == Ops_IN) {
      out->a_kind = XK_GPR; out->a_idx = 0;             /* accumulator */
      out->b_kind = XK_NONE;
      out->port_imm = (uint8_t)(base.imm == IMM_8);
    } else if (op == Ops_OUT) {
      out->a_kind = XK_GPR; out->a_idx = 0;
      out->b_kind = XK_NONE;
      out->port_imm = (uint8_t)(base.imm == IMM_8);
    } else {
      out->a_kind = XK_NONE; out->b_kind = XK_NONE;
    }
    out->opsz = out->io_sz;
  }
  if (op >= Ops_ADD && op <= Ops_CMP && !base.modrm && base.imm != 0) {
    /* Accumulator-immediate forms: A = AL/AX/EAX/RAX, B = immediate. */
    out->a_kind = XK_GPR; out->a_idx = 0;
    out->b_kind = XK_IMM; out->b_idx = 0;
  }
  if (op == Ops_MOV_SEG) {
    /* 8C: r/m16 <- sreg (2-bit selector in the ModRM reg field).
       8E: sreg <- r/m16. The segment index (0..5) lives in `cond`. */
    static const int segmap[8] = {0, 1, 2, 3, 4, 5, -1, -1};
    int sreg = segmap[reg];
    out->cond = (uint8_t)(sreg < 0 ? 0xff : sreg);
    /* 8C writes the 16-bit selector into r/m; a 32-bit GPR destination is
       zero-extended (and REX.W writes 64 bits), so keep the decoded operand
       size for the register form. 8E always reads a 16-bit word. */
    if (dir == 0) out->memsz = 2; else out->opsz = 2;
    out->a_kind = XK_SEG; out->a_idx = (uint8_t)(sreg < 0 ? 0 : sreg);
    out->b_kind = rm_is_reg ? XK_GPR : XK_MEM;
    out->b_idx = rm | (rexB << 3);
    out->illegal = (sreg < 0);
  }
  if (is_x87) {
    /* `simd` carries the memory operand width (0 = register form); for the
       register form the ST(i) index lives in `st_idx` (NOT in `imm`, which is
       the immediate-encoding field and would add a phantom immediate byte). */
    out->simd = gi.form;
    if (gi.has_st) { out->b_kind = XK_X87; out->b_idx = gi.st_idx; }
    else if (gi.form != 0) { out->b_kind = XK_MEM; out->b_idx = 0; out->memsz = gi.form; }
    else { out->b_kind = XK_NONE; out->b_idx = 0; }
    /* DF E0 is FNSTSW AX: the only register-destination form.  (reg == 4 and
       rm == 0, not the other way round -- the old test never matched, so
       FNSTSW AX silently wrote to memory at a stale EA.) */
    if (op == Ops_FNSTSW && mod == 3 && reg == 4 && rm == 0) {
      out->a_kind = XK_GPR;
      out->a_idx = 0;
    }
  }
  if (op == Ops_SHLD || op == Ops_SHRD) {
    /* SHLD/SHRD r/m, r, imm8|CL: A = destination r/m, B = source register,
       shift count is in `imm`. */
    out->a_kind = rm_is_reg ? XK_GPR : XK_MEM; out->a_idx = rm | (rexB << 3);
    out->b_kind = XK_GPR; out->b_idx = reg | (rexR << 3);
    if (opcode == 0xa5 || opcode == 0xad) out->imm = 0;   /* count in CL */
    out->illegal = 0;
  }
  if (op == Ops_POPCNT) {
    out->a_kind = XK_GPR; out->a_idx = reg | (rexR << 3);
    out->b_kind = rm_is_reg ? XK_GPR : XK_MEM; out->b_idx = rm | (rexB << 3);
    out->illegal = 0;
  }
  if (op == Ops_MOVNTI) {
    out->a_kind = rm_is_reg ? XK_GPR : XK_MEM; out->a_idx = rm | (rexB << 3);
    out->b_kind = XK_GPR; out->b_idx = reg | (rexR << 3);
    out->illegal = 0;
  }
  if (op == Ops_LAR) {
    out->a_kind = XK_GPR; out->a_idx = reg | (rexR << 3);
    out->b_kind = rm_is_reg ? XK_GPR : XK_MEM; out->b_idx = rm | (rexB << 3);
    out->opsz = 2;
    out->illegal = 0;
  }
  if (op == Ops_RDRAND || op == Ops_RDSEED || op == Ops_RDFSBASE ||
      op == Ops_RDGSBASE || op == Ops_WRFSBASE || op == Ops_WRGSBASE) {
    out->a_kind = XK_GPR; out->a_idx = rm | (rexB << 3);
    out->b_kind = XK_NONE;
    out->illegal = 0;
  }
  if (op == Ops_XSAVES || op == Ops_XSAVEC || op == Ops_XRSTORS ||
      op == Ops_LKGS) {
    out->a_kind = XK_MEM; out->b_kind = XK_NONE;
    out->illegal = 0;
  }
  if (op == Ops_PREFETCH) {
    out->a_kind = rm_is_reg ? XK_GPR : XK_MEM;
    out->a_idx = rm | (rexB << 3);
    out->b_kind = XK_NONE;
    out->illegal = 0;
  }
  if (op == Ops_LLDT || op == Ops_VERR || op == Ops_VERW) {
    out->a_kind = rm_is_reg ? XK_GPR : XK_MEM;
    out->a_idx = rm | (rexB << 3);
    out->b_kind = XK_NONE;
    out->opsz = 2;
    out->memsz = 2;
    out->illegal = 0;
  }
  if (op == Ops_BSWAP) {
    out->a_kind = XK_GPR; out->a_idx = opLow3 | (rexB << 3); out->b_kind = XK_NONE;
    out->illegal = 0;
  }
  if (op == Ops_SGDT || op == Ops_SIDT || op == Ops_SMSW || op == Ops_LMSW ||
      op == Ops_SLDT || op == Ops_STR) {
    out->a_kind = rm_is_reg ? XK_GPR : XK_MEM;
    out->a_idx = rm | (rexB << 3);
    out->b_kind = XK_NONE;
    out->opsz = 2;                 /* these all read/write a 16-bit word */
    out->memsz = 2;
    out->illegal = 0;
  }
  if (op == Ops_LTR) {

    out->a_kind = XK_GPR; out->a_idx = rm | (rexB << 3);
    out->b_kind = XK_NONE;
    out->opsz = 2;
  }
  if (base.imm == IMM_ADDR && !is0F) {
    /* MOV moffs: absolute address, no registers involved. */
    int is_store = (opcode == 0xa2 || opcode == 0xa3);
    int byte_form = (opcode == 0xa0 || opcode == 0xa2);
    out->opsz = byte_form ? 1 : opsz_bytes;
    out->memsz = out->opsz;
    out->disp = (int64_t)imm;
    out->has_base = 0;
    out->has_index = 0;
    out->rip_rel = 0;
    if (is_store) {
      out->a_kind = XK_MEM; out->a_idx = 0;
      out->b_kind = XK_GPR; out->b_idx = 0;
    } else {
      out->a_kind = XK_GPR; out->a_idx = 0;
      out->b_kind = XK_MEM; out->b_idx = 0;
    }
    out->illegal = 0;
  }
  if (op == Ops_SIMD_CMP) {
    out->a_kind = XK_XMM; out->a_idx = reg | (rexR << 3);
    out->b_kind = rm_is_reg ? XK_XMM : XK_MEM; out->b_idx = rm | (rexB << 3);
    out->simd = p66 ? SIMD_PD : (rep == 2 ? SIMD_SD : (rep == 1 ? SIMD_SS : SIMD_PS));
    out->memsz = out->simd == SIMD_SS ? 4 : (out->simd == SIMD_SD ? 8 : 16);
    out->opsz = 16;
    out->illegal = 0;
  }
  if (op == Ops_PSHIFT_IMM) {
    out->a_kind = XK_XMM; out->a_idx = rm | (rexB << 3);
    out->b_kind = XK_NONE;
    out->opsz = 16;
    out->memsz = 16;
    out->cond = (uint8_t)(reg | ((opcode - 0x70) << 4));
    out->illegal = 0;
  }
  if (op == Ops_PMOVMSKB) {
    out->a_kind = XK_GPR; out->a_idx = reg | (rexR << 3);
    out->b_kind = rm_is_reg ? XK_XMM : XK_MEM; out->b_idx = rm | (rexB << 3);
  }
  if (op == Ops_BSF || op == Ops_BSR) {
    out->a_kind = XK_GPR; out->a_idx = reg | (rexR << 3);
    out->b_kind = rm_is_reg ? XK_GPR : XK_MEM; out->b_idx = rm | (rexB << 3);
  }
  if (op == Ops_BT || op == Ops_BTS || op == Ops_BTR || op == Ops_BTC) {
    out->a_kind = rm_is_reg ? XK_GPR : XK_MEM; out->a_idx = rm | (rexB << 3);
    if (is0F && opcode == 0xba) out->b_kind = XK_IMM;
    else { out->b_kind = XK_GPR; out->b_idx = reg | (rexR << 3); }
  }
  if (op == Ops_CMPXCHG) {
    out->a_kind = rm_is_reg ? XK_GPR : XK_MEM; out->a_idx = rm | (rexB << 3);
    out->b_kind = XK_GPR; out->b_idx = reg | (rexR << 3);
  }
  if (op == Ops_CMPXCHG8B) {
    out->a_kind = XK_MEM; out->a_idx = rm | (rexB << 3);
    out->b_kind = XK_NONE;
    out->memsz = rexW ? 16 : 8;
  }
  if (op == Ops_LIDT) {
    out->a_kind = rm_is_reg ? XK_NONE : XK_MEM; out->a_idx = rm | (rexB << 3);
    out->b_kind = XK_NONE; out->memsz = 10;
  }
  if (op == Ops_INVLPG) {
    out->a_kind = XK_MEM; out->a_idx = rm | (rexB << 3);
    out->b_kind = XK_NONE; out->memsz = 1;
  }
  if (op == Ops_CPUID) { out->a_kind = XK_GPR; out->a_idx = 0; out->b_kind = XK_NONE; }
  if (op == Ops_MOV_CR) {
    out->a_kind = dir == 1 ? XK_NONE : XK_GPR; out->a_idx = rm | (rexB << 3);
    out->b_kind = XK_NONE; out->imm = (uint64_t)reg;
    out->cond = (dir == 1) ? 1 : 0;
    out->illegal = 0;
  }
  if (op == Ops_MOV_DR) {
    out->a_kind = dir == 1 ? XK_NONE : XK_GPR; out->a_idx = rm | (rexB << 3);
    out->b_kind = XK_NONE; out->imm = (uint64_t)reg;
    out->cond = (dir == 1) ? 1 : 0;
    out->illegal = 0;
  }
  if (op == Ops_RDMSR) { out->a_kind = XK_GPR; out->a_idx = 1; out->b_kind = XK_NONE; }
  if (op == Ops_WRMSR) { out->a_kind = XK_GPR; out->a_idx = 1; out->b_kind = XK_GPR; out->b_idx = 0; }
  if (op == Ops_SYSRET) { out->a_kind = XK_GPR; out->a_idx = 1; out->b_kind = XK_GPR; out->b_idx = 11; }
  if (op == Ops_SYSCALL || op == Ops_IRET) { out->a_kind = XK_NONE; out->b_kind = XK_NONE; }
  out->seg_ovr = (uint8_t)seg_ovr;
  (void)lock; (void)sse_sel; (void)p67; (void)b_idx;

  int bad_sse = is_xmm && (op == Ops_SIMD_PAND || op == Ops_SIMD_POR || op == Ops_SIMD_PXOR ||
                           op == Ops_SIMD_PADDD || op == Ops_SIMD_PSUBD || op == Ops_MOVDQ ||
                           op == Ops_MOVQ) && !(p66 || rep == 1);
  /* Legacy high-byte registers: without REX, ModRM reg codes 4..7 name
     AH/CH/DH/BH instead of SPL/BPL/SIL/DIL. Mark them as indices 16..19 so
     the executor reads/writes bits 8..15. */
  if (!rex_valid) {
    int a_byte = (out->opsz == 1);
    int b_byte = (out->opsz == 1);
    if (op == Ops_MOVZX || op == Ops_MOVSX || op == Ops_MOVSXD) {
      a_byte = 0;
      b_byte = (out->memsz == 1);
    }
    if (a_byte && out->a_kind == XK_GPR && out->a_idx >= 4 && out->a_idx <= 7)
      out->a_idx = (uint8_t)(out->a_idx + 12);  /* 4..7 -> 16..19 (AH/CH/DH/BH) */
    if (b_byte && out->b_kind == XK_GPR && out->b_idx >= 4 && out->b_idx <= 7)
      out->b_idx = (uint8_t)(out->b_idx + 12);  /* 4..7 -> 16..19 (AH/CH/DH/BH) */
  }
  out->illegal = is3 || overflow || inst_len > 15 || op == Ops_UNDEF ||
                 (op == Ops_MOVSXD && !rexW) ||
                 (op == Ops_MOV_CR && !rm_is_reg) ||
                 (op == Ops_IRET && !rexW) || bad_sse;
  ce->rip = rip;
  ce->gen = xemu_code_gen;
  ce->inst = *out;
  /* Remember that this page holds code, so a later store to it invalidates. */
  xemu_code_page_mark(pa);
  return 0;
}
