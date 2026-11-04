#include "xemu.h"
#include "ops.h"
#include <string.h>

/* Immediate classes (mirror ImmKind). */
enum { IMM_NONE = 0, IMM_8 = 1, IMM_Z = 2, IMM_32 = 3, IMM_64 = 4, IMM_16 = 5 };
/* Decode forms (mirror DecodeForm). */
enum { FORM_NORMAL = 0, FORM_MOVZX8 = 1, FORM_MOVZX16 = 2, FORM_MOVSX8 = 3,
       FORM_MOVSX16 = 4, FORM_MOVSXD = 5, FORM_ACC = 6, FORM_SPECIAL = 7 };
/* Opcode groups (mirror OpcodeGroup). */
enum { G_NONE = 0, G_1 = 1, G_2 = 2, G_3 = 3, G_4 = 4, G_5 = 5, G_11 = 6, G_1A = 7 };
/* SIMD kinds (mirror SimdKind). */
enum { SIMD_NONE = 0, SIMD_SS = 1, SIMD_SD = 2, SIMD_PS = 3, SIMD_PD = 4, SIMD_DQ = 5 };

typedef struct {
  uint8_t op, modrm, imm, dir, group, force8, form, sse_sel, store;
} DInfo;

static DInfo primary[256], twobyte[256], groups[7][8];
static int tables_ready;

#define DI(opv, ...) ((DInfo){ .op = (opv), __VA_ARGS__ })

static void set(DInfo *t, int i, DInfo d) { t[i] = d; }

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
  primary[0x8d] = DI(Ops_LEA, .modrm = 1, .dir = 1);
  primary[0x8f] = DI(Ops_UNDEF, .modrm = 1, .group = G_1A);
  primary[0x90] = DI(Ops_NOP, .modrm = 0);
  for (int r = 1; r < 8; r++) primary[0x90 + r] = DI(Ops_XCHG, .modrm = 0, .dir = 1, .form = FORM_SPECIAL);
  primary[0x98] = DI(Ops_CBW, .modrm = 0);
  primary[0x99] = DI(Ops_CDQ, .modrm = 0);
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
  primary[0xc3] = DI(Ops_RET, .modrm = 0);
  primary[0xc6] = DI(Ops_UNDEF, .modrm = 1, .group = G_11, .imm = IMM_8, .force8 = 1);
  primary[0xc7] = DI(Ops_UNDEF, .modrm = 1, .group = G_11, .imm = IMM_Z);
  primary[0xc8] = DI(Ops_UNDEF, .modrm = 0, .imm = IMM_16);
  primary[0xc9] = DI(Ops_UNDEF, .modrm = 0);
  primary[0xcc] = DI(Ops_INT3, .modrm = 0);
  primary[0xcd] = DI(Ops_INT, .modrm = 0, .imm = IMM_8);
  primary[0xcf] = DI(Ops_IRET, .modrm = 0);
  primary[0xd0] = DI(Ops_UNDEF, .modrm = 1, .group = G_2, .force8 = 1);
  primary[0xd1] = DI(Ops_UNDEF, .modrm = 1, .group = G_2);
  primary[0xd2] = DI(Ops_UNDEF, .modrm = 1, .group = G_2, .force8 = 1);
  primary[0xd3] = DI(Ops_UNDEF, .modrm = 1, .group = G_2);
  primary[0xd7] = DI(Ops_UNDEF, .modrm = 0);
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
  set(twobyte, 0x30, DI(Ops_WRMSR, .modrm = 0));
  set(twobyte, 0x32, DI(Ops_RDMSR, .modrm = 0));
  set(twobyte, 0x0b, DI(Ops_UD2, .modrm = 0));
  set(twobyte, 0x01, DI(Ops_UNDEF, .modrm = 1));
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

int xemu_decode(uint64_t rip, XemuInst *out) {
  init_tables();
  uint8_t buf[16];
  uint64_t pa, ferr;
  if (xemu_translate(rip, 0, 1, &pa, &ferr) != 0) {
    xemu_raise_exc(14, ferr, rip);
    return 1; /* fetched exception */
  }
  if (xemu_vread(rip, buf, 16) != 0) return -1;
  memset(out, 0, sizeof(*out));

  int off = 0, p66 = 0, p67 = 0, rep = 0, lock = 0, rex = 0, rex_valid = 0;
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
  int g_active = has_modrm && base.group != 0;
  if (g_active) gi = groups[base.group - 1][reg];

  int imm_kind = (g_active && gi.imm != 0) ? gi.imm : base.imm;
  int dir = g_active ? gi.dir : base.dir;
  int form = g_active ? gi.form : base.form;
  int op = is3 ? Ops_UNDEF : (g_active ? gi.op : base.op);
  int force8 = base.force8;
  int sse_sel = base.sse_sel && !g_active;

  int isE6 = is0F && opcode == 0xe6 && !is3;
  int is5A = is0F && opcode == 0x5a && !is3;
  int is2A = is0F && opcode == 0x2a && !is3;
  int is2C = is0F && opcode == 0x2c && !is3;
  int is2D = is0F && opcode == 0x2d && !is3;
  int is5B = is0F && opcode == 0x5b && !is3;
  int isLidt = is0F && opcode == 0x01 && !is3 && reg == 3;
  int isInvlpg = is0F && opcode == 0x01 && !is3 && reg == 7;
  if (isE6) op = p66 ? Ops_CVTTPD2DQ : (rep == 1 ? Ops_CVTDQ2PD : (rep == 2 ? Ops_CVTPD2DQ : Ops_UNDEF));
  else if (is5A) op = rep == 1 ? Ops_CVTSS2SD : (rep == 2 ? Ops_CVTSD2SS : Ops_UNDEF);
  else if (is2A) op = rep == 1 ? Ops_CVTSI2SS : (rep == 2 ? Ops_CVTSI2SD : Ops_UNDEF);
  else if (is2C) op = rep == 1 ? Ops_CVTTSS2SI : (rep == 2 ? Ops_CVTTSD2SI : Ops_UNDEF);
  else if (is2D) op = rep == 2 ? Ops_CVTSD2SI : Ops_UNDEF;
  else if (is5B) op = p66 ? Ops_CVTPS2DQ : Ops_CVTDQ2PS;
  else if (isLidt) op = Ops_LIDT;
  else if (isInvlpg) op = Ops_INVLPG;
  if (is0F && opcode == 0xba) {
    static const int btops[8] = {0,0,0,0,Ops_BT,Ops_BTS,Ops_BTR,Ops_BTC};
    op = btops[reg];
    imm_kind = IMM_8;
    dir = 0;
  }
  if (is0F && opcode == 0xc7 && reg == 1) {
    op = Ops_CMPXCHG8B;
    dir = 0;
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
    default: imm = 0; break;
  }
  int inst_len = imm_off + imm_len;

  out->len = inst_len;
  out->op = op;
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
  if (op == Ops_RDMSR) { out->a_kind = XK_GPR; out->a_idx = 1; out->b_kind = XK_NONE; }
  if (op == Ops_WRMSR) { out->a_kind = XK_GPR; out->a_idx = 1; out->b_kind = XK_GPR; out->b_idx = 0; }
  if (op == Ops_SYSRET) { out->a_kind = XK_GPR; out->a_idx = 1; out->b_kind = XK_GPR; out->b_idx = 11; }
  if (op == Ops_SYSCALL || op == Ops_IRET) { out->a_kind = XK_NONE; out->b_kind = XK_NONE; }
  (void)lock; (void)sse_sel; (void)p67; (void)b_idx;

  int bad_sse = is_xmm && (op == Ops_SIMD_PAND || op == Ops_SIMD_POR || op == Ops_SIMD_PXOR ||
                           op == Ops_SIMD_PADDD || op == Ops_SIMD_PSUBD || op == Ops_MOVDQ ||
                           op == Ops_MOVQ) && !(p66 || rep == 1);
  out->illegal = is3 || overflow || inst_len > 15 || op == Ops_UNDEF ||
                 (op == Ops_MOVSXD && !rexW) ||
                 (op == Ops_MOV_CR && !rm_is_reg) ||
                 (op == Ops_IRET && !rexW) || bad_sse;
  return 0;
}
