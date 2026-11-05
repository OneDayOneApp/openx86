#include "xemu.h"
#include "ops.h"
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

enum { S_SS = 1, S_SD = 2, S_PS = 3, S_PD = 4, S_DQ = 5 };

typedef union {
  uint64_t u[2];
  uint32_t w[4];
  float f[4];
  double d[2];
} Vec128;

static Vec128 get_vec(int idx) {
  Vec128 v;
  v.u[0] = xemu.xmm[idx][0];
  v.u[1] = xemu.xmm[idx][1];
  return v;
}
static void put_vec(int idx, Vec128 v) {
  xemu.xmm[idx][0] = v.u[0];
  xemu.xmm[idx][1] = v.u[1];
}
static Vec128 zero_vec(void) { Vec128 v; v.u[0] = v.u[1] = 0; return v; }

static int ld_vec(uint64_t addr, int n, Vec128 *v) {
  uint8_t b[16] = {0};
  if (xemu_vread(addr, b, n) != 0) {
    if (!xemu_fault_pending) {
      xemu_raise_exc(14, xemu_fault_err, xemu.rip);
      xemu_fault_pending = 1;
    }
    return -1;
  }
  *v = zero_vec();
  memcpy(v->u, b, n);
  return 0;
}
static void st_vec(uint64_t addr, int n, Vec128 v) {
  uint8_t b[16];
  memcpy(b, v.u, 16);
  xemu_vwrite(addr, b, n);
}

static uint64_t ea(const XemuInst *d) {
  if (d->rip_rel) return xemu.rip + d->len + (uint64_t)d->disp;
  uint64_t a = 0;
  if (d->has_base) a += xemu.gpr[d->base];
  if (d->has_index) a += xemu.gpr[d->index] << d->scale;
  return a + (uint64_t)d->disp;
}

static Vec128 src_vec(const XemuInst *d) {
  if (d->b_kind == XK_XMM) return get_vec(d->b_idx);
  if (d->b_kind == XK_MEM) {
    Vec128 v;
    ld_vec(ea(d), d->memsz, &v);
    return v;
  }
  Vec128 v = zero_vec();
  v.u[0] = d->b_kind == XK_GPR ? xemu.gpr[d->b_idx] : d->imm;
  return v;
}

static float qnan_f32(void) { return *(float *)&(uint32_t){0xffc00000u}; }
static double qnan_f64(void) { return *(double *)&(uint64_t){0xfff8000000000000ull}; }

static float canon_f32(float x) {
  uint32_t b; memcpy(&b, &x, 4);
  if ((b & 0x7f800000u) == 0x7f800000u && (b & 0x007fffffu)) return qnan_f32();
  return x;
}
static double canon_f64(double x) {
  uint64_t b; memcpy(&b, &x, 8);
  if ((b & 0x7ff0000000000000ull) == 0x7ff0000000000000ull && (b & 0xfffffffffffffull)) return qnan_f64();
  return x;
}

static float f32_add(float a, float b) { return canon_f32(a + b); }
static float f32_sub(float a, float b) { return canon_f32(a - b); }
static float f32_mul(float a, float b) { return canon_f32(a * b); }
static float f32_div(float a, float b) { return canon_f32(a / b); }
static double f64_add(double a, double b) { return canon_f64(a + b); }
static double f64_sub(double a, double b) { return canon_f64(a - b); }
static double f64_mul(double a, double b) { return canon_f64(a * b); }
static double f64_div(double a, double b) { return canon_f64(a / b); }

static float f32_min(float a, float b) {
  if (isnan(a) || isnan(b)) return b;
  if (a < b) return a;
  if (a > b) return b;
  return b;
}
static float f32_max(float a, float b) {
  if (isnan(a) || isnan(b)) return b;
  if (a > b) return a;
  if (a < b) return b;
  return b;
}
static double f64_min(double a, double b) {
  if (isnan(a) || isnan(b)) return b;
  if (a < b) return a;
  if (a > b) return b;
  return b;
}
static double f64_max(double a, double b) {
  if (isnan(a) || isnan(b)) return b;
  if (a > b) return a;
  if (a < b) return b;
  return b;
}

static void set_szp(uint64_t res, int sz) {
  uint64_t m = sz >= 8 ? ~0ull : ((1ull << (sz * 8)) - 1);
  res &= m;
  xemu.rflags &= ~((1ull << 6) | (1ull << 7) | (1ull << 2));
  if (res == 0) xemu.rflags |= 1ull << 6;
  if (res & (1ull << (sz * 8 - 1))) xemu.rflags |= 1ull << 7;
  uint8_t p = (uint8_t)res;
  int par = 1;
  for (int i = 0; i < 8; i++) par ^= (p >> i) & 1;
  if (par) xemu.rflags |= 1ull << 2;
}

static void comis_flags(double a, double b) {
  const uint64_t mask = (1ull << 0) | (1ull << 2) | (1ull << 4) | (1ull << 6) |
                        (1ull << 7) | (1ull << 11);
  xemu.rflags &= ~mask;
  if (isnan(a) || isnan(b)) { xemu.rflags |= (1ull << 0) | (1ull << 2) | (1ull << 6); return; }
  if (a < b) xemu.rflags |= 1ull << 0;
  else if (a == b) xemu.rflags |= 1ull << 6;
}

static uint64_t get_lane(Vec128 v, int idx, int bytes) {
  uint64_t m = bytes == 8 ? ~0ull : ((1ull << (bytes * 8)) - 1);
  int bit = idx * bytes * 8;
  uint64_t w = bit >= 64 ? v.u[1] : v.u[0];
  return (w >> (bit & 63)) & m;
}
static void set_lane(Vec128 *v, int idx, int bytes, uint64_t x) {
  uint64_t m = bytes == 8 ? ~0ull : ((1ull << (bytes * 8)) - 1);
  int bit = idx * bytes * 8;
  uint64_t *w = bit >= 64 ? &v->u[1] : &v->u[0];
  int sh = bit & 63;
  *w = (*w & ~(m << sh)) | ((x & m) << sh);
}

int xemu_sse_exec(const XemuInst *d) {
  uint16_t op = d->op;
  int simd = d->simd;
  Vec128 dst = (d->a_kind == XK_XMM) ? get_vec(d->a_idx) : zero_vec();
  Vec128 src = src_vec(d);
  if (getenv("SSETRACE"))
    fprintf(stderr, "[sse] rip=0x%llx op=%u simd=%u a=%u/%u b=%u/%u opsz=%u\n",
            (unsigned long long)xemu.rip, (unsigned)op, (unsigned)simd,
            (unsigned)d->a_kind, (unsigned)d->a_idx, (unsigned)d->b_kind,
            (unsigned)d->b_idx, (unsigned)d->opsz);
  Vec128 out = dst;
  uint64_t addr = ea(d);

  switch (op) {
    case Ops_MOVD:
      if (d->a_kind == XK_XMM) {
        out = zero_vec();
        uint64_t v = (d->b_kind == XK_GPR) ? xemu.gpr[d->b_idx] : src.u[0];
        if (d->opsz == 8) out.u[0] = v;
        else out.w[0] = (uint32_t)v;
        put_vec(d->a_idx, out);
      } else {
        uint64_t v = src.u[0];
        if (d->a_kind == XK_GPR) {
          if (d->opsz == 8) xemu.gpr[d->a_idx] = v;
          else xemu.gpr[d->a_idx] = v & 0xffffffff;
        } else st_vec(addr, d->opsz == 8 ? 8 : 4, src);
      }
      break;
    case Ops_MOVQ:
      if (d->a_kind == XK_XMM) {
        out = zero_vec();
        out.u[0] = d->b_kind == XK_GPR ? xemu.gpr[d->b_idx] : src.u[0];
        put_vec(d->a_idx, out);
      } else {
        uint64_t v = src.u[0];
        if (d->a_kind == XK_GPR) xemu.gpr[d->a_idx] = v;
        else st_vec(addr, 8, src);
      }
      break;
    case Ops_MOVDQ:
    case Ops_MOVPS:
      if (d->a_kind == XK_XMM) {
        if (simd == S_SS) {
          out = dst; out.f[0] = src.f[0];
          if (d->b_kind == XK_MEM) { out.u[1] = 0; }
        } else if (simd == S_SD) {
          out = dst; out.u[0] = src.u[0];
          if (d->b_kind == XK_MEM) out.u[1] = 0;
        } else out = src;
        put_vec(d->a_idx, out);
      } else if (d->a_kind == XK_MEM) {
        int n = op == Ops_MOVDQ ? 16 : (simd == S_SS ? 4 : (simd == S_SD ? 8 : 16));
        st_vec(addr, n, src);
      }
      break;
    /* andps/andnps/orps/xorps: the non-66 forms of the packed bitwise ops.
       They were decoded (0F 54-57) but had no case here, so any program that
       used them died with "unsupported op=8x" -- busybox's `ls` is the first
       one we hit. Results are bitwise and do not depend on the element type. */
    case Ops_SIMD_AND: case Ops_SIMD_ANDN: case Ops_SIMD_OR: case Ops_SIMD_XOR: {
      Vec128 r = zero_vec();
      for (int i = 0; i < 2; i++) {
        r.u[i] = op == Ops_SIMD_AND  ? (dst.u[i] & src.u[i])
               : op == Ops_SIMD_ANDN ? ((~dst.u[i]) & src.u[i])
               : op == Ops_SIMD_OR   ? (dst.u[i] | src.u[i])
                                     : (dst.u[i] ^ src.u[i]);
      }
      put_vec(d->a_idx, r);
      break;
    }
    case Ops_SIMD_PAND: case Ops_SIMD_POR: case Ops_SIMD_PXOR: {
      Vec128 r = zero_vec();
      for (int i = 0; i < 2; i++) {
        r.u[i] = op == Ops_SIMD_PAND ? (dst.u[i] & src.u[i])
                : op == Ops_SIMD_POR  ? (dst.u[i] | src.u[i])
                                      : (dst.u[i] ^ src.u[i]);
      }
      put_vec(d->a_idx, r);
      break;
    }
    case Ops_PADDB: case Ops_PADDW: case Ops_PADDQ:
    case Ops_PSUBB: case Ops_PSUBW: case Ops_PSUBQ: {
      int bytes = (op == Ops_PADDB || op == Ops_PSUBB) ? 1 :
                  (op == Ops_PADDW || op == Ops_PSUBW) ? 2 :
                  (op == Ops_PADDQ || op == Ops_PSUBQ) ? 8 : 4;
      Vec128 r = zero_vec();
      for (int i = 0; i < 16 / bytes; i++) {
        uint64_t m = bytes == 8 ? ~0ull : ((1ull << (bytes * 8)) - 1);
        uint64_t av = get_lane(dst, i, bytes);
        uint64_t bv = get_lane(src, i, bytes);
        uint64_t rv = (op == Ops_PADDB || op == Ops_PADDW || op == Ops_PADDQ)
                        ? (av + bv) & m : (av - bv) & m;
        set_lane(&r, i, bytes, rv);
      }
      put_vec(d->a_idx, r);
      break;
    }
    case Ops_PCMPEQB: case Ops_PCMPEQW: case Ops_PCMPEQD:
    case Ops_PCMPGTB: case Ops_PCMPGTW: case Ops_PCMPGTD: {
      int bytes = (op == Ops_PCMPEQB || op == Ops_PCMPGTB) ? 1 :
                  (op == Ops_PCMPEQW || op == Ops_PCMPGTW) ? 2 : 4;
      Vec128 r = zero_vec();
      for (int i = 0; i < 16 / bytes; i++) {
        uint64_t m = (1ull << (bytes * 8)) - 1;
        uint64_t av = get_lane(dst, i, bytes);
        uint64_t bv = get_lane(src, i, bytes);
        int eq = (av == bv);
        int gt = 0;
        if (bytes == 1) gt = (int8_t)av > (int8_t)bv;
        else if (bytes == 2) gt = (int16_t)av > (int16_t)bv;
        else gt = (int32_t)av > (int32_t)bv;
        int take = (op == Ops_PCMPEQB || op == Ops_PCMPEQW || op == Ops_PCMPEQD) ? eq : gt;
        set_lane(&r, i, bytes, take ? m : 0);
      }
      put_vec(d->a_idx, r);
      break;
    }
    case Ops_PSHUFD: {
      Vec128 r;
      uint8_t imm = (uint8_t)d->imm;
      r.w[0] = src.w[(imm >> 0) & 3];
      r.w[1] = src.w[(imm >> 2) & 3];
      r.w[2] = src.w[(imm >> 4) & 3];
      r.w[3] = src.w[(imm >> 6) & 3];
      put_vec(d->a_idx, r);
      break;
    }
    /* pshufhw/pshuflw shuffle one 64-bit half (4 words) and copy the other
       half through. Vec128 only exposes 4 dwords, so work on a uint16 view:
       indexing w[4..7] here would run past the union and corrupt the heap. */
    case Ops_PSHUFHW: case Ops_PSHUFLW: {
      uint16_t h[8], r[8];
      memcpy(h, src.u, sizeof(h));
      memcpy(r, h, sizeof(r));
      uint8_t imm = (uint8_t)d->imm;
      int base = (op == Ops_PSHUFHW) ? 4 : 0;
      for (int i = 0; i < 4; i++)
        r[base + i] = h[base + ((imm >> (2 * i)) & 3)];
      Vec128 out = zero_vec();
      memcpy(out.u, r, sizeof(r));
      put_vec(d->a_idx, out);
      break;
    }
    case Ops_PUNPCKLBW: case Ops_PUNPCKLWD: case Ops_PUNPCKLDQ: case Ops_PUNPCKLQDQ:
    case Ops_PUNPCKHBW: case Ops_PUNPCKHWD: case Ops_PUNPCKHDQ: case Ops_PUNPCKHQDQ: {
      int bytes = (op == Ops_PUNPCKLBW || op == Ops_PUNPCKHBW) ? 1 :
                  (op == Ops_PUNPCKLWD || op == Ops_PUNPCKHWD) ? 2 :
                  (op == Ops_PUNPCKLDQ || op == Ops_PUNPCKHDQ) ? 4 : 8;
      int high = (op == Ops_PUNPCKHBW || op == Ops_PUNPCKHWD ||
                  op == Ops_PUNPCKHDQ || op == Ops_PUNPCKHQDQ);
      int lanes = 16 / bytes;
      Vec128 r = zero_vec();
      for (int i = 0; i < lanes / 2; i++) {
        int ai = (high ? lanes / 2 : 0) + i;
        int bi = (high ? lanes / 2 : 0) + i;
        uint64_t av = get_lane(dst, ai, bytes);
        uint64_t bv = get_lane(src, bi, bytes);
        int oi = i * 2;
        set_lane(&r, oi, bytes, av);
        set_lane(&r, oi + 1, bytes, bv);
      }
      put_vec(d->a_idx, r);
      break;
    }
    case Ops_PMOVMSKB: {
      uint32_t mask = 0;
      for (int i = 0; i < 16; i++)
        mask |= (uint32_t)((get_lane(src, i, 1) >> 7) & 1) << i;
      xemu.gpr[d->a_idx] = mask;
      break;
    }
    case Ops_MOVNTDQ:
      st_vec(addr, 16, src);
      break;
    case Ops_SIMD_PADDD: case Ops_SIMD_PSUBD: {
      Vec128 r = zero_vec();
      for (int i = 0; i < 4; i++)
        r.w[i] = op == Ops_SIMD_PADDD ? (dst.w[i] + src.w[i]) : (dst.w[i] - src.w[i]);
      put_vec(d->a_idx, r);
      break;
    }
    case Ops_SIMD_ADD: case Ops_SIMD_SUB: case Ops_SIMD_MUL:
    case Ops_SIMD_DIV: case Ops_SIMD_MIN: case Ops_SIMD_MAX:
    case Ops_SIMD_SQRT: {
      Vec128 r = dst;
      if (simd == S_SS) {
        float a = dst.f[0], b = src.f[0], x = 0;
        switch (op) {
          case Ops_SIMD_ADD: x = f32_add(a, b); break;
          case Ops_SIMD_SUB: x = f32_sub(a, b); break;
          case Ops_SIMD_MUL: x = f32_mul(a, b); break;
          case Ops_SIMD_DIV: x = f32_div(a, b); break;
          case Ops_SIMD_MIN: x = f32_min(a, b); break;
          case Ops_SIMD_MAX: x = f32_max(a, b); break;
          case Ops_SIMD_SQRT: x = canon_f32(sqrtf(b)); break;
        }
        r.f[0] = x;
      } else if (simd == S_SD) {
        double a = dst.d[0], b = src.d[0], x = 0;
        switch (op) {
          case Ops_SIMD_ADD: x = f64_add(a, b); break;
          case Ops_SIMD_SUB: x = f64_sub(a, b); break;
          case Ops_SIMD_MUL: x = f64_mul(a, b); break;
          case Ops_SIMD_DIV: x = f64_div(a, b); break;
          case Ops_SIMD_MIN: x = f64_min(a, b); break;
          case Ops_SIMD_MAX: x = f64_max(a, b); break;
          case Ops_SIMD_SQRT: x = canon_f64(sqrt(b)); break;
        }
        r.d[0] = x;
      } else {
        int lanes = (simd == S_PS) ? 4 : 2;
        for (int i = 0; i < lanes; i++) {
          if (simd == S_PS) {
            float a = dst.f[i], b = src.f[i], x = 0;
            switch (op) {
              case Ops_SIMD_ADD: x = f32_add(a, b); break;
              case Ops_SIMD_SUB: x = f32_sub(a, b); break;
              case Ops_SIMD_MUL: x = f32_mul(a, b); break;
              case Ops_SIMD_DIV: x = f32_div(a, b); break;
              case Ops_SIMD_MIN: x = f32_min(a, b); break;
              case Ops_SIMD_MAX: x = f32_max(a, b); break;
              case Ops_SIMD_SQRT: x = canon_f32(sqrtf(b)); break;
            }
            r.f[i] = x;
          } else {
            double a = dst.d[i], b = src.d[i], x = 0;
            switch (op) {
              case Ops_SIMD_ADD: x = f64_add(a, b); break;
              case Ops_SIMD_SUB: x = f64_sub(a, b); break;
              case Ops_SIMD_MUL: x = f64_mul(a, b); break;
              case Ops_SIMD_DIV: x = f64_div(a, b); break;
              case Ops_SIMD_MIN: x = f64_min(a, b); break;
              case Ops_SIMD_MAX: x = f64_max(a, b); break;
              case Ops_SIMD_SQRT: x = canon_f64(sqrt(b)); break;
            }
            r.d[i] = x;
          }
        }
      }
      put_vec(d->a_idx, r);
      break;
    }
    case Ops_SIMD_COMI: case Ops_SIMD_UCOMI: {
      if (simd == S_SS) comis_flags((double)dst.f[0], (double)src.f[0]);
      else comis_flags(dst.d[0], src.d[0]);
      break;
    }
    case Ops_CVTSI2SS: case Ops_CVTSI2SD: {
      int64_t iv = (int64_t)(d->b_kind == XK_GPR ? xemu.gpr[d->b_idx] : src.u[0]);
      if (d->opsz == 4) iv = (int32_t)iv;
      Vec128 r = dst;
      if (op == Ops_CVTSI2SS) r.f[0] = canon_f32((float)iv);
      else r.d[0] = canon_f64((double)iv);
      put_vec(d->a_idx, r);
      break;
    }
    case Ops_CVTTSS2SI: case Ops_CVTTSD2SI: case Ops_CVTSD2SI: {
      double v = (simd == S_SD) ? src.d[0] : (double)src.f[0];
      if (d->opsz == 4) {
        if (isnan(v) || v < -2147483648.0 || v > 2147483647.0)
          xemu.gpr[d->a_idx] = 0x80000000ull;
        else
          xemu.gpr[d->a_idx] = (uint32_t)(int32_t)v;
      } else {
        if (isnan(v) || v < -9223372036854775808.0 || v >= 9223372036854775808.0)
          xemu.gpr[d->a_idx] = 0x8000000000000000ull;
        else
          xemu.gpr[d->a_idx] = (uint64_t)(int64_t)v;
      }
      break;
    }
    case Ops_CVTSS2SD: {
      Vec128 r = dst;
      uint32_t b = src.w[0];
      if ((b & 0x7f800000u) == 0x7f800000u && (b & 0x7fffffu)) {
        uint64_t sign = (uint64_t)(b >> 31) << 63;
        uint64_t mant = (uint64_t)(b & 0x7fffffu) << 29;
        uint64_t dbl = sign | (0x7ffull << 52) | mant;
        dbl |= (1ull << 51); /* result is always quieted */
        r.u[0] = dbl;
      } else {
        r.d[0] = canon_f64((double)src.f[0]);
      }
      put_vec(d->a_idx, r);
      break;
    }
    case Ops_CVTSD2SS: {
      Vec128 r = dst;
      uint64_t b = src.u[0];
      if ((b & 0x7ff0000000000000ull) == 0x7ff0000000000000ull &&
          (b & 0xfffffffffffffull)) {
        uint32_t sign = (uint32_t)(b >> 63) << 31;
        uint32_t mant = (uint32_t)((b >> 29) & 0x7fffffu);
        uint32_t flt = sign | (0xffu << 23) | mant;
        flt |= (1u << 22); /* result is always quieted */
        r.w[0] = flt;
      } else {
        r.f[0] = canon_f32((float)src.d[0]);
      }
      put_vec(d->a_idx, r);
      break;
    }
    case Ops_CVTDQ2PS: {
      Vec128 r;
      for (int i = 0; i < 4; i++) r.f[i] = canon_f32((float)(int32_t)src.w[i]);
      put_vec(d->a_idx, r);
      break;
    }
    case Ops_CVTPS2DQ: {
      Vec128 r;
      for (int i = 0; i < 4; i++) r.w[i] = (uint32_t)(int32_t)lrintf(src.f[i]);
      put_vec(d->a_idx, r);
      break;
    }
    case Ops_CVTDQ2PD: {
      Vec128 r = zero_vec();
      r.d[0] = canon_f64((double)(int32_t)src.w[0]);
      r.d[1] = canon_f64((double)(int32_t)src.w[1]);
      put_vec(d->a_idx, r);
      break;
    }
    case Ops_CVTPD2DQ: {
      Vec128 r = zero_vec();
      r.w[0] = (uint32_t)(int32_t)lrint(src.d[0]);
      r.w[1] = (uint32_t)(int32_t)lrint(src.d[1]);
      put_vec(d->a_idx, r);
      break;
    }
    case Ops_CVTTPD2DQ: {
      Vec128 r = zero_vec();
      r.w[0] = (uint32_t)(int32_t)src.d[0];
      r.w[1] = (uint32_t)(int32_t)src.d[1];
      put_vec(d->a_idx, r);
      break;
    }
    default:
      (void)set_szp;
      return -1;
  }
  xemu.rip += d->len;
  return 0;
}
