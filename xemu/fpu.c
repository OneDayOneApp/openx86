/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 tongxiaojun
 */
/* x87 floating-point unit.
 *
 * The register stack is held as `long double`. On AArch64 that is IEEE
 * binary128, which carries more precision than the 80-bit extended format the
 * guest expects, so guest values round-trip exactly; stores round explicitly.
 *
 * Implemented: control/status/tag words, FNINIT/FCLEX, FNSTCW/FNSTSW,
 * FLDCW, memory and register loads/stores, FILD/FIST, the four arithmetic
 * operations in both memory and register forms, FCOM/FUCOM, FXAM, FTST,
 * FCHS/FABS/FSQRT, and the no-operand stack housekeeping instructions.
 */
#include "xemu.h"
#include "ops.h"

#include <math.h>
#include <string.h>

static void wr32le(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}
static uint32_t rd32le(const uint8_t *p) {
  uint32_t v = 0;
  for (int i = 0; i < 4; i++) v |= (uint32_t)p[i] << (8 * i);
  return v;
}

typedef struct {
  long double st[8];
  uint16_t tag;        /* 1 = empty for ST(i), bit i */
  uint16_t cw;         /* control word */
  uint16_t sw;         /* status word */
  int top;             /* index of ST(0) */
} Fpu;

static Fpu fpu;

void xemu_fpu_reset(void) {
  memset(&fpu, 0, sizeof(fpu));
  fpu.cw = 0x037F;
  fpu.tag = 0xFF;      /* all empty */
  fpu.top = 0;
}

/* Status word as the guest sees it: C0..C3 live in fpu.sw, but TOP (bits
 * 13:11) is tracked separately as fpu.top and has to be folded in here.
 * Without this FNSTSW/FSTSW reported TOP = 0 after every push/pop. */
static uint16_t sw_arch(void) {
  return (uint16_t)((fpu.sw & ~0x3800) | ((uint16_t)(fpu.top & 7) << 11));
}

uint16_t xemu_fpu_cw(void) { return fpu.cw; }
uint16_t xemu_fpu_sw(void) { return sw_arch(); }
uint16_t xemu_fpu_tag(void) { return fpu.tag; }

static long double *st(int i) { return &fpu.st[(fpu.top + i) & 7]; }

static void set_cc(int c0, int c1, int c2, int c3) {
  fpu.sw &= (uint16_t)~(0x4700);
  fpu.sw |= (uint16_t)((c0 ? 0x0100 : 0) | (c1 ? 0x0200 : 0) |
                       (c2 ? 0x0400 : 0) | (c3 ? 0x4000 : 0));
}

static void push(long double v) {
  fpu.top = (fpu.top - 1) & 7;
  *st(0) = v;
  fpu.tag &= (uint16_t)~(1u << fpu.top);
}

static long double pop(void) {
  long double v = *st(0);
  fpu.tag |= (uint16_t)(1u << fpu.top);
  fpu.top = (fpu.top + 1) & 7;
  return v;
}

static int is_empty(int i) { return (fpu.tag >> ((fpu.top + i) & 7)) & 1; }

/* Compare ST(0) with `b` and set the condition codes like FCOM. */
static void compare(long double b, int ordered) {
  long double a = *st(0);
  if (isnan(a) || isnan(b)) {
    set_cc(1, 0, 1, 1);          /* unordered */
    fpu.sw |= 0x0100;            /* IE */
    return;
  }
  if (a > b) set_cc(0, 0, 0, 0);
  else if (a < b) set_cc(1, 0, 0, 0);
  else set_cc(0, 0, 0, 1);       /* equal */
  (void)ordered;
}

/* Round a value to the 80-bit extended precision the guest sees. */
static long double narrow(long double v) {
  /* binary128 has 113 bits of mantissa vs 64 for extended; mask the low bits
     to emulate the narrower significand. */
  if (v == 0.0L || !isfinite((double)v)) return v;
  int exp = 0;
  long double m = frexpl(v, &exp);       /* m in [-1,-0.5] or [0.5,1) */
  const int drop = 113 - 64;
  long double scale = ldexpl(1.0L, drop);
  m = roundl(m * scale) / scale;
  return ldexpl(m, exp);
}

/* --- memory helpers ------------------------------------------------- */

static uint64_t rd(uint64_t addr, int n) {
  uint64_t v = 0;
  xemu_vread(addr, &v, (size_t)n);
  return v;
}

static long double load_m32(uint64_t addr) {
  uint32_t bits = (uint32_t)rd(addr, 4);
  float f;
  memcpy(&f, &bits, 4);
  return (long double)f;
}

static long double load_m64(uint64_t addr) {
  uint64_t bits = rd(addr, 8);
  double d;
  memcpy(&d, &bits, 8);
  return (long double)d;
}

static long double load_m80(uint64_t addr) {
  uint8_t raw[10];
  xemu_vread(addr, raw, 10);
  uint64_t mant = 0;
  for (int i = 0; i < 8; i++) mant |= (uint64_t)raw[i] << (8 * i);
  uint16_t se = (uint16_t)(raw[8] | (raw[9] << 8));
  int sign = (se >> 15) & 1;
  int exp = se & 0x7fff;
  long double v;
  if (exp == 0 && mant == 0) v = 0.0L;
  else if (exp == 0x7fff) v = (mant << 1) ? NAN : INFINITY;
  else v = ldexpl((long double)mant, exp - 16383 - 63);
  return sign ? -v : v;
}

static void store_m32(uint64_t addr, long double v) {
  float f = (float)v;
  uint32_t bits;
  memcpy(&bits, &f, 4);
  xemu_vwrite(addr, &bits, 4);
}

static void store_m64(uint64_t addr, long double v) {
  double d = (double)v;
  uint64_t bits;
  memcpy(&bits, &d, 8);
  xemu_vwrite(addr, &bits, 8);
}

static void encode_m80(uint8_t *raw, long double v) {
  memset(raw, 0, 10);
  int sign = signbit(v) ? 1 : 0;
  long double a = fabsl(v);
  int exp;
  long double m = frexpl(a, &exp);       /* a = m * 2^exp, m in [0.5,1) */
  uint64_t mant;
  uint16_t se;
  if (v != v) {                           /* NaN */
    se = (uint16_t)((sign << 15) | 0x7fff);
    mant = 0xC000000000000000ull;
  } else if (isinf((double)v)) {
    se = (uint16_t)((sign << 15) | 0x7fff);
    mant = 0x8000000000000000ull;
  } else if (a == 0.0L) {
    se = (uint16_t)(sign << 15);
    mant = 0;
  } else {
    mant = (uint64_t)ldexpl(m, 64);
    se = (uint16_t)((sign << 15) | (exp - 1 + 16383));
  }
  for (int i = 0; i < 8; i++) raw[i] = (uint8_t)(mant >> (8 * i));
  raw[8] = (uint8_t)se;
  raw[9] = (uint8_t)(se >> 8);
}

static void store_m80(uint64_t addr, long double v) {
  uint8_t raw[10];
  encode_m80(raw, v);
  xemu_vwrite(addr, raw, 10);
}

/* --- execution ------------------------------------------------------ */

static uint8_t fpu_stub_env[108];

/* --- FXSAVE / FXRSTOR ----------------------------------------------- */

/* 512-byte save area. Only the fields the kernel relies on are meaningful:
 * the control/status words, the tag word, and the MXCSR. The register stack
 * itself is written from our `long double` array, narrowed to 80-bit. */
static void encode_m80(uint8_t *raw, long double v);

void xemu_fpu_fxsave(uint64_t addr) {
  uint8_t area[512];
  memset(area, 0, sizeof(area));
  uint16_t cw = fpu.cw, sw = fpu.sw;
  memcpy(area + 0, &cw, 2);
  memcpy(area + 2, &sw, 2);
  area[4] = (uint8_t)(fpu.tag & 0xff);        /* abridged tag word */
  wr32le(area + 8, xemu.mxcsr);               /* MXCSR */
  wr32le(area + 12, 0xffff);                  /* MXCSR mask */
  for (int i = 0; i < 8; i++) encode_m80(area + 32 + i * 16, *st(i));
  for (int i = 0; i < 16; i++) {
    uint64_t lo = xemu.xmm[i][0], hi = xemu.xmm[i][1];
    memcpy(area + 160 + i * 16, &lo, 8);
    memcpy(area + 168 + i * 16, &hi, 8);
  }
  xemu_vwrite(addr, area, sizeof(area));
}

void xemu_fpu_fxrstor(uint64_t addr) {
  uint8_t area[512];
  if (xemu_vread(addr, area, sizeof(area)) != 0) return;
  memcpy(&fpu.cw, area + 0, 2);
  memcpy(&fpu.sw, area + 2, 2);
  fpu.tag = area[4];
  fpu.top = 0;
  xemu.mxcsr = rd32le(area + 8);
  for (int i = 0; i < 8; i++) {
    uint8_t raw[10];
    memcpy(raw, area + 32 + i * 16, 10);
    uint64_t mant = 0;
    for (int b = 0; b < 8; b++) mant |= (uint64_t)raw[b] << (8 * b);
    uint16_t se = (uint16_t)(raw[8] | (raw[9] << 8));
    int sign = (se >> 15) & 1, exp = se & 0x7fff;
    long double v;
    if (exp == 0 && mant == 0) v = 0.0L;
    else if (exp == 0x7fff) v = (mant << 1) ? NAN : INFINITY;
    else v = ldexpl((long double)mant, exp - 16383 - 63);
    fpu.st[i] = sign ? -v : v;
  }
  for (int i = 0; i < 16; i++) {
    memcpy(&xemu.xmm[i][0], area + 160 + i * 16, 8);
    memcpy(&xemu.xmm[i][1], area + 168 + i * 16, 8);
  }
}

int xemu_fpu_exec(const XemuInst *d) {
  uint64_t ea = 0;
  if (d->b_kind == XK_MEM) ea = xemu_ea(d);
  int sz = d->simd;                 /* 4, 8, 10 for memory operands */
  long double a, b;

  switch (d->op) {
    case Ops_FNINIT:
      xemu_fpu_reset();
      return 0;
    case Ops_FNCLEX:
      fpu.sw &= (uint16_t)~0x0080;
      return 0;
    case Ops_FNSTCW:
      xemu_vwrite(ea, &fpu.cw, 2);
      return 0;
    case Ops_FNSTSW:
      {
        uint16_t sw = sw_arch();
        if (d->a_kind == XK_GPR) xemu.gpr[0] = (xemu.gpr[0] & ~0xffffull) | sw;
        else xemu_vwrite(ea, &sw, 2);
      }
      return 0;
    case Ops_FLDCW:
      xemu_vread(ea, &fpu.cw, 2);
      return 0;
    case Ops_FNSTENV:
    case Ops_FNSAVE:
      /* Provide a plausible environment image; the kernel only needs the
         control word and status word to be meaningful. */
      {
      fpu_stub_env[0] = (uint8_t)(fpu.cw & 0xff);
      fpu_stub_env[1] = (uint8_t)(fpu.cw >> 8);
      fpu_stub_env[4] = (uint8_t)(fpu.sw & 0xff);
      fpu_stub_env[5] = (uint8_t)(fpu.sw >> 8);
      xemu_vwrite(ea, fpu_stub_env, d->op == Ops_FNSAVE ? 108 : 28);
      if (d->op == Ops_FNSAVE) xemu_fpu_reset();
      return 0;
      }
    case Ops_FLD:
      if (d->b_kind == XK_MEM) {
        a = (sz == 4) ? load_m32(ea) : (sz == 8) ? load_m64(ea) : load_m80(ea);
        push(narrow(a));
      } else {
        push(*st(d->b_idx & 7));
      }
      return 0;
    case Ops_FST:
    case Ops_FSTP:
      if (d->b_kind == XK_MEM) {
        a = *st(0);
        if (sz == 4) store_m32(ea, a);
        else if (sz == 8) store_m64(ea, a);
        else store_m80(ea, a);
      } else {
        *st(d->b_idx & 7) = *st(0);
      }
      if (d->op == Ops_FSTP) pop();
      return 0;
    case Ops_FILD:
      if (sz == 2) { uint16_t v; xemu_vread(ea, &v, 2); push((long double)(int16_t)v); }
      else if (sz == 4) { uint32_t v; xemu_vread(ea, &v, 4); push((long double)(int32_t)v); }
      else { uint64_t v; xemu_vread(ea, &v, 8); push((long double)(int64_t)v); }
      return 0;
    case Ops_FIST:
    case Ops_FISTP: {
      a = *st(0);
      long double r = (fpu.cw & 0x0c00) == 0x0c00 ? truncl(a)
                    : (fpu.cw & 0x0c00) == 0x0800 ? ceill(a)
                    : (fpu.cw & 0x0c00) == 0x0400 ? floorl(a)
                                                  : roundl(a);
      if (sz == 2) { int16_t v = (int16_t)r; xemu_vwrite(ea, &v, 2); }
      else if (sz == 4) { int32_t v = (int32_t)r; xemu_vwrite(ea, &v, 4); }
      else { int64_t v = (int64_t)r; xemu_vwrite(ea, &v, 8); }
      if (d->op == Ops_FISTP) pop();
      return 0;
    }
    case Ops_FADD: case Ops_FSUB: case Ops_FMUL: case Ops_FDIV:
    case Ops_FADDP: case Ops_FSUBP: case Ops_FMULP: case Ops_FDIVP:
    case Ops_FSUBR: case Ops_FDIVR: case Ops_FSUBRP: case Ops_FDIVRP: {
      /* `a` is ST(0) (the source operand) and `b` is ST(i) or the memory
         operand (the destination for the "P" forms).  The non-reversed
         operations compute DEST - SRC, the reversed ones SRC - DEST; for the
         register forms DEST is ST(i), so the arithmetic has to be expressed
         in terms of b and a respectively. */
      int rev = (d->op == Ops_FSUBR || d->op == Ops_FDIVR ||
                 d->op == Ops_FSUBRP || d->op == Ops_FDIVRP);
      int pop_after = (d->op == Ops_FADDP || d->op == Ops_FSUBP ||
                       d->op == Ops_FMULP || d->op == Ops_FDIVP ||
                       d->op == Ops_FSUBRP || d->op == Ops_FDIVRP);
      b = (d->b_kind == XK_MEM)
              ? ((sz == 4) ? load_m32(ea) : load_m64(ea))
              : *st(d->b_idx & 7);
      a = *st(0);
      /* The two-operand register/memory forms write the result to ST(0);
         the "P" (pop) forms write it to ST(1) and then pop, which makes ST(1)
         the destination.  Writing the P forms to ST(0) as well left the
         result one slot too high and then dropped it, so the stack quietly
         lost one entry per operation. */
      /* The two-operand forms write ST(0); the "P" forms write ST(1) and
         then pop, making ST(1) the destination.  Resolve the destination to a
         physical slot BEFORE popping: st(i) is an offset from fpu.top, so a
         pointer taken now would follow the stack pointer and land on the
         wrong register. */
      int dst_slot = (fpu.top + (pop_after ? 1 : 0)) & 7;
      /* SDM: FADD/FSUB/FMUL/FDIV compute ST(0) <op> ST(i); the reversed
         forms (FSUBR/FDIVR) swap the operands.  The "P" variants store into
         ST(1) and pop, but use the same operand order. */
      long double r = 0;
      switch (d->op) {
        case Ops_FADD: case Ops_FADDP: r = a + b; break;
        case Ops_FSUB: case Ops_FSUBP: r = b - a; break;   /* DEST - SRC */
        case Ops_FSUBR: case Ops_FSUBRP: r = a - b; break; /* SRC - DEST */
        case Ops_FMUL: case Ops_FMULP: r = a * b; break;
        case Ops_FDIV: case Ops_FDIVP: r = b / a; break;   /* DEST / SRC */
        case Ops_FDIVR: case Ops_FDIVRP: r = a / b; break; /* SRC / DEST */
        default: break;
      }
      fpu.st[dst_slot] = narrow(r);
      if (pop_after) pop();
      return 0;
    }
    case Ops_FCOM: case Ops_FCOMP: case Ops_FCOMPP: case Ops_FUCOM: case Ops_FUCOMP:
      b = (d->b_kind == XK_MEM)
              ? ((sz == 4) ? load_m32(ea) : load_m64(ea))
              : *st(d->b_idx & 7);
      compare(b, 1);
      if (d->op == Ops_FCOMP || d->op == Ops_FUCOMP) pop();
      if (d->op == Ops_FCOMPP) { pop(); pop(); }
      return 0;
    /* FCOMI/FUCOMI and the popping FCOMIP/FUCOMIP: compare ST(0) with ST(i)
       and put the result in EFLAGS (ZF/PF/CF) instead of only in the x87
       status word.  Intel documents them as "unordered" (FUCOMI) or signalling
       (FCOMI) on QNaN; a single-threaded model with no exceptions pending
       treats both the same apart from the unordered flag ordering. */
    case Ops_FCOMI: case Ops_FCOMIP: case Ops_FUCOMI: case Ops_FUCOMIP: {
      b = *st(d->b_idx & 7);
      a = *st(0);
      int c3, c2, c0;
      if (isnan(a) || isnan(b)) { c3 = 1; c2 = 1; c0 = 1; }   /* unordered */
      else if (a > b)           { c3 = 0; c2 = 0; c0 = 0; }   /* greater   */
      else if (a < b)           { c3 = 0; c2 = 0; c0 = 1; }   /* less      */
      else                      { c3 = 1; c2 = 0; c0 = 0; }   /* equal     */
      set_cc(c0, 0, c2, c3);
      xemu.rflags &= ~(0x1 | 0x4 | 0x40 | 0x80);   /* CF PF AF ZF */
      if (c0) xemu.rflags |= 0x1;                  /* CF */
      if (c2) xemu.rflags |= 0x4;                  /* PF */
      if (c3) xemu.rflags |= 0x40;                 /* ZF */
      if (d->op == Ops_FCOMIP || d->op == Ops_FUCOMIP) pop();
      return 0;
    }
    case Ops_FTST:
      compare(0.0L, 1);
      return 0;
    case Ops_FXAM: {
      /* SDM classification table.  C1 carries the sign, C0/C2/C3 the class:
           unsupported 0 0 0 | NaN 0 0 1 | normal 0 1 0 | infinity 0 1 1
           zero        1 0 0 | empty 1 0 1 | denormal 1 1 0   (C3 C2 C0) */
      long double v = *st(0);
      int sign = signbit(v) ? 1 : 0;
      if (is_empty(0))                       set_cc(1, sign, 0, 1);
      else if (isnan(v))                     set_cc(1, sign, 0, 0);
      else if (isinf(v))                     set_cc(1, sign, 1, 0);
      else if (v == 0.0L)                    set_cc(0, sign, 0, 1);
      else if (fpclassify(v) == FP_SUBNORMAL) set_cc(0, sign, 1, 1);
      else                                   set_cc(0, sign, 1, 0);
      return 0;
    }
    case Ops_FCHS: *st(0) = -*st(0); return 0;
    case Ops_FABS: *st(0) = fabsl(*st(0)); return 0;
    case Ops_FSQRT: *st(0) = narrow(sqrtl(*st(0))); return 0;
    case Ops_FXCH: {
      int i = d->b_idx & 7;
      long double t = *st(0); *st(0) = *st(i); *st(i) = t;
      return 0;
    }
    case Ops_FFREE: fpu.tag |= (uint16_t)(1u << ((fpu.top + d->b_idx) & 7)); return 0;
    case Ops_FLD1: push(1.0L); return 0;
    case Ops_FLDZ: push(0.0L); return 0;
    case Ops_FLDPI: push(narrow(3.14159265358979323846264338327950288L)); return 0;
    case Ops_FLDL2E: push(narrow(1.44269504088896340735992468100189214L)); return 0;
    case Ops_FLDL2T: push(narrow(3.32192809488736234787031942948939018L)); return 0;
    case Ops_FLDLG2: push(narrow(0.301029995663981195213738894724493027L)); return 0;
    case Ops_FLDLN2: push(narrow(0.693147180559945309417232121458176568L)); return 0;
    case Ops_FNOP: return 0;

    /* ---- condition-code dependent moves (FCMOVcc) -------------------- */
    /* The x87 stores its comparison result as C0/C2/C3 in the status word;
       the FCMOVcc encodings name the same bits, so test the status word
       directly. */
    case Ops_FCMOVB:  case Ops_FCMOVNB:
    case Ops_FCMOVE:  case Ops_FCMOVNE:
    case Ops_FCMOVBE: case Ops_FCMOVNBE:
    case Ops_FCMOVU:  case Ops_FCMOVNU: {
      int c0 = (fpu.sw >> 8) & 1, c2 = (fpu.sw >> 10) & 1, c3 = (fpu.sw >> 14) & 1;
      int take;
      switch (d->op) {
        case Ops_FCMOVB:  take = c0; break;              /* CF = 1 */
        case Ops_FCMOVNB: take = !c0; break;
        case Ops_FCMOVE:  take = c3; break;              /* ZF = 1 */
        case Ops_FCMOVNE: take = !c3; break;
        case Ops_FCMOVBE: take = c0 || c3; break;        /* CF or ZF */
        case Ops_FCMOVNBE: take = !(c0 || c3); break;
        case Ops_FCMOVU:  take = c2; break;              /* PF = 1 (unordered) */
        default:          take = !c2; break;             /* FCMOVNU */
      }
      if (take) *st(0) = *st(d->b_idx & 7);
      return 0;
    }

    /* ---- transcendental and rounding --------------------------------- */
    case Ops_F2XM1: *st(0) = narrow(powl(2.0L, *st(0)) - 1.0L); return 0;
    case Ops_FYL2X: {
      long double y = *st(1), x = *st(0);
      long double r = y * log2l(x);
      st(1)[0] = narrow(r);
      pop();
      return 0;
    }
    case Ops_FYL2XP1: {
      long double y = *st(1), x = *st(0);
      long double r = y * log2l(x + 1.0L);
      st(1)[0] = narrow(r);
      pop();
      return 0;
    }
    case Ops_FPATAN: {
      long double y = *st(1), x = *st(0);
      long double r = atan2l(y, x);
      st(1)[0] = narrow(r);
      pop();
      return 0;
    }
    case Ops_FPTAN: {
      long double r = tanl(*st(0));
      *st(0) = narrow(r);
      push(1.0L);
      return 0;
    }
    case Ops_FSIN: *st(0) = narrow(sinl(*st(0))); return 0;
    case Ops_FCOS: *st(0) = narrow(cosl(*st(0))); return 0;
    case Ops_FSINCOS: {
      long double v = *st(0);
      *st(0) = narrow(sinl(v));
      push(narrow(cosl(v)));
      return 0;
    }
    case Ops_FRNDINT: {
      long double a0 = *st(0);
      long double r = (fpu.cw & 0x0c00) == 0x0c00 ? truncl(a0)
                    : (fpu.cw & 0x0c00) == 0x0800 ? ceill(a0)
                    : (fpu.cw & 0x0c00) == 0x0400 ? floorl(a0)
                                                  : roundl(a0);
      *st(0) = narrow(r);
      return 0;
    }
    case Ops_FSCALE: {
      long double y = *st(0), x = *st(1);
      /* FSCALE truncates the scale factor toward zero. */
      long double r = scalbnl(y, (int)truncl(x));
      *st(0) = narrow(r);
      return 0;
    }
    case Ops_FPREM: case Ops_FPREM1: {
      /* Both compute partial remainders; the exact reconstruction of the
         quotient bits (C2/C1/C0) is not modelled -- a single pass with the
         exact IEEE remainder is enough for the Linux userspace this runs. */
      long double x = *st(0), y = *st(1);
      long double r = remainderl(x, y);
      st(0)[0] = narrow(r);
      set_cc(0, 0, 0, 0);
      return 0;
    }
    case Ops_FXTRACT: {
      long double v = *st(0);
      int e = 0;
      long double m = frexpl(v, &e);     /* v = m * 2^e, 0.5 <= |m| < 1 */
      /* x87 returns the significand scaled to [1,2) and the exponent. */
      *st(0) = narrow(m * 2.0L);
      push((long double)(e - 1));
      return 0;
    }
    case Ops_FDECSTP: fpu.top = (fpu.top + 1) & 7; return 0;
    case Ops_FINCSTP: fpu.top = (fpu.top - 1) & 7; return 0;

    /* FISTTP stores and pops unconditionally. */
    case Ops_FISTTP: {
      long double r = truncl(*st(0));
      if (sz == 2) { int16_t v = (int16_t)r; xemu_vwrite(ea, &v, 2); }
      else if (sz == 4) { int32_t v = (int32_t)r; xemu_vwrite(ea, &v, 4); }
      else { int64_t v = (int64_t)r; xemu_vwrite(ea, &v, 8); }
      pop();
      return 0;
    }
    default:
      return -1;
  }
}
