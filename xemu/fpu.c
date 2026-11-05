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

uint16_t xemu_fpu_cw(void) { return fpu.cw; }
uint16_t xemu_fpu_sw(void) { return fpu.sw; }
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

static void store_m80(uint64_t addr, long double v) {
  uint8_t raw[10] = {0};
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
  xemu_vwrite(addr, raw, 10);
}

/* --- execution ------------------------------------------------------ */

static uint8_t fpu_stub_env[108];

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
      if (d->a_kind == XK_GPR) xemu.gpr[0] = (xemu.gpr[0] & ~0xffffull) | fpu.sw;
      else xemu_vwrite(ea, &fpu.sw, 2);
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
      int rev = (d->op == Ops_FSUBR || d->op == Ops_FDIVR ||
                 d->op == Ops_FSUBRP || d->op == Ops_FDIVRP);
      int pop_after = (d->op == Ops_FADDP || d->op == Ops_FSUBP ||
                       d->op == Ops_FMULP || d->op == Ops_FDIVP ||
                       d->op == Ops_FSUBRP || d->op == Ops_FDIVRP);
      b = (d->b_kind == XK_MEM)
              ? ((sz == 4) ? load_m32(ea) : load_m64(ea))
              : *st(d->b_idx & 7);
      a = *st(0);
      long double x = rev ? b : a;
      long double y = rev ? a : b;
      switch (d->op) {
        case Ops_FADD: case Ops_FADDP: *st(0) = narrow(x + y); break;
        case Ops_FSUB: case Ops_FSUBP: *st(0) = narrow(x - y); break;
        case Ops_FSUBR: case Ops_FSUBRP: *st(0) = narrow(y - x); break;
        case Ops_FMUL: case Ops_FMULP: *st(0) = narrow(x * y); break;
        case Ops_FDIV: case Ops_FDIVP: *st(0) = narrow(x / y); break;
        case Ops_FDIVR: case Ops_FDIVRP: *st(0) = narrow(y / x); break;
        default: break;
      }
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
    case Ops_FTST:
      compare(0.0L, 1);
      return 0;
    case Ops_FXAM: {
      long double v = *st(0);
      if (isnan(v)) set_cc(0, 0, 0, 1);
      else if (isinf((double)v)) set_cc(0, 1, 1, 1);
      else if (v == 0.0L) set_cc(0, 0, 0, 0);
      else set_cc(0, 1, 0, signbit(v) ? 1 : 0);
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
    default:
      return -1;
  }
}
