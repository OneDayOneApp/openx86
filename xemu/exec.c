#include "xemu.h"
#include "ops.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* RFLAGS bit positions. */
enum { F_CF = 0, F_PF = 2, F_AF = 4, F_ZF = 6, F_SF = 7, F_TF = 8,
       F_IF = 9, F_DF = 10, F_OF = 11 };

static inline uint64_t mask_of(int sz) {
  return sz >= 8 ? ~0ull : ((1ull << (sz * 8)) - 1);
}
static inline uint64_t sign_of(int sz) { return 1ull << (sz * 8 - 1); }
static inline int bit(uint64_t v, int b) { return (int)((v >> b) & 1); }
static inline void setf(int b, int v) {
  if (v) xemu.rflags |= (1ull << b); else xemu.rflags &= ~(1ull << b);
}
static inline int getf(int b) { return (int)((xemu.rflags >> b) & 1); }

static int parity(uint64_t v) {
  v &= 0xff;
  v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
  return (int)(~v & 1);
}

static void set_szp(uint64_t res, int sz) {
  uint64_t m = mask_of(sz);
  res &= m;
  setf(F_ZF, res == 0);
  setf(F_SF, (res & sign_of(sz)) != 0);
  setf(F_PF, parity(res));
}

/* Indices 16..19 denote the legacy high-byte registers AH/CH/DH/BH, which
 * alias bits 8..15 of RAX/RCX/RDX/RBX and exist only when the instruction has
 * no REX prefix. The decoder rewrites 4..7 to 16..19 in that case. */
static uint64_t read_reg(int idx, int sz) {
  if (idx >= 16) {
    if (sz == 1) return (xemu.gpr[idx - 16] >> 8) & 0xff;
    idx -= 16;
  }
  uint64_t v = xemu.gpr[idx];
  if (sz >= 8) return v;
  return v & mask_of(sz);
}

static void write_reg(int idx, uint64_t val, int sz) {
  if (idx >= 16) {
    if (sz == 1) {
      uint64_t *r = &xemu.gpr[idx - 16];
      *r = (*r & ~0xff00ull) | ((val & 0xff) << 8);
      return;
    }
    idx -= 16;
  }
  if (sz >= 8) { xemu.gpr[idx] = val; return; }
  if (sz == 4) { xemu.gpr[idx] = val & 0xffffffffull; return; }
  uint64_t m = mask_of(sz);
  xemu.gpr[idx] = (xemu.gpr[idx] & ~m) | (val & m);
}

uint64_t xemu_ea(const XemuInst *d) {
  if (d->rip_rel) {
    uint64_t a = xemu.rip + d->len + (uint64_t)d->disp;
    /* FS/GS overrides add their base even for RIP-relative operands. */
    if (d->seg_ovr == 5) a += xemu.fs_base;
    else if (d->seg_ovr == 6) a += xemu.gs_base;
    return a;
  }
  uint64_t a = 0;
  if (d->has_base) a += xemu.gpr[d->base];
  if (d->has_index) a += xemu.gpr[d->index] << d->scale;
  a += (uint64_t)d->disp;
  /* In 64-bit mode only FS and GS keep a non-zero base (TLS). */
  if (d->seg_ovr == 5) a += xemu.fs_base;
  else if (d->seg_ovr == 6) a += xemu.gs_base;
  return a;
}

/* RSP at the start of the current instruction.  A faulting stack write must
 * be rolled back: hardware delivers #PF with the frame of the *unmodified*
 * instruction (RIP = the faulting instruction, RSP = its entry value), which
 * is what Linux's fixup and restart paths rely on. */
static uint64_t inst_entry_rsp;

static uint64_t ld(uint64_t addr, int sz) {
  uint8_t b[8] = {0};
  if (xemu_vread(addr, b, sz) != 0) {
    if (!xemu_fault_pending) {
      xemu.gpr[4] = inst_entry_rsp;
      xemu_raise_exc(14, xemu_fault_err, xemu.rip);
      xemu_fault_pending = 1;
    }
    return 0;
  }
  uint64_t v = 0;
  for (int i = 0; i < sz; i++) v |= (uint64_t)b[i] << (8 * i);
  return v;
}

static void st(uint64_t addr, uint64_t v, int sz) {
  uint8_t b[8];
  for (int i = 0; i < sz; i++) b[i] = (uint8_t)(v >> (8 * i));
  if (xemu_vwrite(addr, b, sz) != 0) {
    if (!xemu_fault_pending) {
      xemu.gpr[4] = inst_entry_rsp;   /* roll the stack back before the frame */
      xemu_raise_exc(14, xemu_fault_err, xemu.rip);
      xemu_fault_pending = 1;
    }
  }
}

static uint64_t read_op(const XemuInst *d, int is_a, int sz) {
  int kind = is_a ? d->a_kind : d->b_kind;
  int idx = is_a ? d->a_idx : d->b_idx;
  if (kind == XK_GPR) return read_reg(idx, sz);
  if (kind == XK_MEM) return ld(xemu_ea(d), sz);
  if (kind == XK_IMM) return d->imm & mask_of(sz);
  return 0;
}

static void write_op(const XemuInst *d, int is_a, uint64_t v, int sz) {
  int kind = is_a ? d->a_kind : d->b_kind;
  int idx = is_a ? d->a_idx : d->b_idx;
  if (kind == XK_GPR) write_reg(idx, v, sz);
  else if (kind == XK_MEM) st(xemu_ea(d), v, sz);
}

static void add_flags(uint64_t a, uint64_t b, uint64_t res, int sz, int cin) {
  unsigned __int128 full = (unsigned __int128)a + b + cin;
  int bits = sz * 8;
  setf(F_CF, bits >= 64 ? (int)((full >> 64) & 1) : (int)((full >> bits) & 1));
  uint64_t m = mask_of(sz);
  setf(F_AF, ((a ^ b ^ res) & 0x10) != 0);
  setf(F_OF, ((~(a ^ b) & (a ^ res)) & sign_of(sz)) != 0);
  (void)m;
}

static void sub_flags(uint64_t a, uint64_t b, uint64_t res, int sz, int bin) {
  unsigned __int128 sub = (unsigned __int128)b + bin;
  int bits = sz * 8;
  setf(F_CF, (unsigned __int128)a < sub);
  setf(F_AF, ((a ^ b ^ res) & 0x10) != 0);
  setf(F_OF, (((a ^ b) & (a ^ res)) & sign_of(sz)) != 0);
  (void)bits;
}

static void logic_flags(uint64_t res, int sz) {
  setf(F_CF, 0); setf(F_OF, 0);
  set_szp(res, sz);
}

static int cond_true(int cc) {
  int cf = getf(F_CF), zf = getf(F_ZF), sf = getf(F_SF), of = getf(F_OF), pf = getf(F_PF);
  switch (cc & 0xf) {
    case 0x0: return of;            case 0x1: return !of;
    case 0x2: return cf;            case 0x3: return !cf;
    case 0x4: return zf;            case 0x5: return !zf;
    case 0x6: return cf || zf;      case 0x7: return !cf && !zf;
    case 0x8: return sf;            case 0x9: return !sf;
    case 0xa: return pf;            case 0xb: return !pf;
    case 0xc: return sf != of;      case 0xd: return sf == of;
    case 0xe: return zf || (sf != of); case 0xf: return !zf && (sf == of);
  }
  return 0;
}

static uint64_t alu_op(uint16_t op, uint64_t a, uint64_t b, int sz) {
  uint64_t res = 0;
  switch (op) {
    case Ops_ADD: res = a + b; add_flags(a, b, res, sz, 0); set_szp(res, sz); return res;
    case Ops_ADC: { int c = getf(F_CF); res = a + b + c; add_flags(a, b, res, sz, c); set_szp(res, sz); return res; }
    case Ops_SUB: case Ops_CMP: res = a - b; sub_flags(a, b, res, sz, 0); set_szp(res, sz); return res;
    case Ops_SBB: { int c = getf(F_CF); res = a - b - c; sub_flags(a, b, res, sz, c); set_szp(res, sz); return res; }
    case Ops_AND: res = a & b; logic_flags(res, sz); return res;
    case Ops_OR:  res = a | b; logic_flags(res, sz); return res;
    case Ops_XOR: res = a ^ b; logic_flags(res, sz); return res;
    case Ops_TEST: res = a & b; logic_flags(res, sz); return res;
    default: return 0;
  }
}

static uint64_t shift_op(uint16_t op, uint64_t a, uint64_t cnt, int sz) {
  int bits = sz * 8;
  unsigned mask = bits == 64 ? 63 : 31;
  unsigned c = (unsigned)(cnt & mask);
  uint64_t m = mask_of(sz);
  uint64_t res = a & m;
  if (c == 0) return res;
  switch (op) {
    case Ops_SHL:
      res = (a << c) & m;
      setf(F_CF, bit(a, bits - c));
      if (c == 1) setf(F_OF, bit(res, bits - 1) ^ getf(F_CF));
      break;
    case Ops_SHR:
      res = (a & m) >> c;
      setf(F_CF, bit(a, c - 1));
      if (c == 1) setf(F_OF, bit(a, bits - 1));
      break;
    case Ops_SAR: {
      int64_t s = (int64_t)(a << (64 - bits)) >> (64 - bits);
      s >>= c;
      res = (uint64_t)s & m;
      setf(F_CF, bit(a, c - 1));
      if (c == 1) setf(F_OF, 0);
      break;
    }
    case Ops_ROL: {
      c %= bits;
      res = c ? (((a << c) | (a >> (bits - c))) & m) : (a & m);
      setf(F_CF, bit(res, 0));
      if (c == 1) setf(F_OF, bit(res, bits - 1) ^ getf(F_CF));
      return res;
    }
    case Ops_ROR: {
      c %= bits;
      res = c ? (((a >> c) | (a << (bits - c))) & m) : (a & m);
      setf(F_CF, bit(res, bits - 1));
      if (c == 1) setf(F_OF, bit(res, bits - 1) ^ bit(res, bits - 2));
      return res;
    }
    case Ops_RCL: {
      c %= (bits + 1);
      uint64_t wide = ((uint64_t)getf(F_CF) << bits) | (a & m);
      int nb = bits + 1;
      for (unsigned i = 0; i < c; i++) wide = ((wide << 1) | (wide >> (nb - 1))) & ((1ull << nb) - 1);
      res = wide & m;
      setf(F_CF, bit(wide, bits));
      if (c == 1) setf(F_OF, bit(res, bits - 1) ^ getf(F_CF));
      return res;
    }
    case Ops_RCR: {
      c %= (bits + 1);
      uint64_t wide = ((uint64_t)getf(F_CF) << bits) | (a & m);
      int nb = bits + 1;
      for (unsigned i = 0; i < c; i++) wide = ((wide >> 1) | ((wide & 1) << (nb - 1))) & ((1ull << nb) - 1);
      res = wide & m;
      setf(F_CF, bit(wide, bits));
      if (c == 1) setf(F_OF, bit(wide, bits - 1) ^ bit(wide, bits));
      return res;
    }
  }
  set_szp(res, sz);
  return res;
}

static void push_val(uint64_t v, int sz) {
  xemu.gpr[4] -= sz;
  st(xemu.gpr[4], v, sz);
  /* st() already rolled RSP back to the instruction's entry value and raised
   * #PF (which installed the handler's own stack in gpr[4]); touching RSP
   * again here would clobber the trap frame pointer. */
}
static uint64_t pop_val(int sz) {
  uint64_t v = ld(xemu.gpr[4], sz);
  if (xemu_fault_pending) return 0;   /* RSP already rolled back by ld */
  xemu.gpr[4] += sz;
  return v;
}

static int str_dir(void) { return getf(F_DF) ? -1 : 1; }

static int do_string(const XemuInst *d) {
  int sz = d->opsz;
  int dir = str_dir();
  int rep = d->rep;
  uint64_t count = rep ? xemu.gpr[1] : 1;
  while (count) {
    uint64_t src = xemu.gpr[6], dst = xemu.gpr[7];
    uint64_t a = 0, b = 0;
    if (d->op == Ops_MOVS) {
      if (xemu_vread(src, &a, sz) != 0) { xemu_raise_exc(14, xemu_fault_err, xemu.rip); return 0; }
      if (xemu_vwrite(dst, &a, sz) != 0) { xemu_raise_exc(14, xemu_fault_err, xemu.rip); return 0; }
      xemu.gpr[6] += dir * sz; xemu.gpr[7] += dir * sz;
    } else if (d->op == Ops_STOS) {
      a = read_reg(0, sz);
      if (xemu_vwrite(dst, &a, sz) != 0) { xemu_raise_exc(14, xemu_fault_err, xemu.rip); return 0; }
      xemu.gpr[7] += dir * sz;
    } else if (d->op == Ops_LODS) {
      if (xemu_vread(src, &a, sz) != 0) { xemu_raise_exc(14, xemu_fault_err, xemu.rip); return 0; }
      write_reg(0, a, sz); xemu.gpr[6] += dir * sz;
    } else if (d->op == Ops_SCAS) {
      if (xemu_vread(dst, &b, sz) != 0) { xemu_raise_exc(14, xemu_fault_err, xemu.rip); return 0; }
      a = read_reg(0, sz);
      uint64_t r = a - b; sub_flags(a, b, r, sz, 0); set_szp(r, sz);
      xemu.gpr[7] += dir * sz;
    } else if (d->op == Ops_CMPS) {
      if (xemu_vread(src, &a, sz) != 0) { xemu_raise_exc(14, xemu_fault_err, xemu.rip); return 0; }
      if (xemu_vread(dst, &b, sz) != 0) { xemu_raise_exc(14, xemu_fault_err, xemu.rip); return 0; }
      uint64_t r = a - b; sub_flags(a, b, r, sz, 0); set_szp(r, sz);
      xemu.gpr[6] += dir * sz; xemu.gpr[7] += dir * sz;
    }
    if (!rep) break;
    count--;
    xemu.gpr[1] = count;
    if ((d->op == Ops_CMPS || d->op == Ops_SCAS)) {
      if (rep == 1 && !getf(F_ZF)) break;
      if (rep == 2 && getf(F_ZF)) break;
    }
  }
  return 0;
}

int xemu_sse_exec(const XemuInst *d);

int xemu_exec(const XemuInst *d) {
  inst_entry_rsp = xemu.gpr[4];
  int sz = d->opsz;
  uint16_t op = d->op;
  if (xemu_fault_pending) { xemu_fault_pending = 0; return 0; }
  xemu_exc_delivered = 0;
  if (op == Ops_FXSAVE) {
    xemu_fpu_fxsave(xemu_ea(d));
    xemu.rip += d->len;
    xemu_after_inst();
    return 0;
  }
  if (op == Ops_FXRSTOR) {
    xemu_fpu_fxrstor(xemu_ea(d));
    xemu.rip += d->len;
    xemu_after_inst();
    return 0;
  }
  if (op == Ops_LDMXCSR || op == Ops_STMXCSR ||
      op == Ops_LFENCE || op == Ops_MFENCE || op == Ops_SFENCE ||
      op == Ops_CLFLUSH || op == Ops_PREFETCH) {
    if (op == Ops_LDMXCSR) xemu.mxcsr = (uint32_t)ld(xemu_ea(d), 4);
    else if (op == Ops_STMXCSR) st(xemu_ea(d), xemu.mxcsr, 4);
    /* fences and cache maintenance are no-ops in a single-threaded model */
    xemu.rip += d->len;
    xemu_after_inst();
    return 0;
  }
  if (op >= Ops_FNINIT && op <= Ops_FNOP) {
    if (xemu_fpu_exec(d) != 0) return -1;
    xemu.rip += d->len;
    xemu_after_inst();
    return 0;
  }
  if ((op >= Ops_MOVD && op <= Ops_CVTTPD2DQ) ||
      (op >= Ops_PADDB && op <= Ops_MOVNTDQ)) return xemu_sse_exec(d);
  switch (op) {
    case Ops_MOVS: case Ops_STOS: case Ops_LODS: case Ops_SCAS: case Ops_CMPS:
      do_string(d);
      break;
    case Ops_IN: {
      uint32_t port = d->port_imm ? (uint32_t)(d->imm & 0xffff)
                                  : (uint32_t)(xemu.gpr[2] & 0xffff);
      uint64_t v = 0;
      xemu_port_in(port, d->io_sz, &v);
      write_reg(0, v, d->io_sz);
      break;
    }
    case Ops_OUT: {
      uint32_t port = d->port_imm ? (uint32_t)(d->imm & 0xffff)
                                  : (uint32_t)(xemu.gpr[2] & 0xffff);
      xemu_port_out(port, d->io_sz, read_reg(0, d->io_sz));
      break;
    }
    case Ops_INS: case Ops_OUTS: {
      int dir = str_dir();
      uint32_t port = (uint32_t)(xemu.gpr[2] & 0xffff);
      uint64_t count = d->rep ? xemu.gpr[1] : 1;
      int sz = d->io_sz ? d->io_sz : 1;
      while (count--) {
        if (op == Ops_INS) {
          uint64_t v = 0;
          xemu_port_in(port, sz, &v);
          if (xemu_vwrite(xemu.gpr[7], &v, sz) != 0) {
            xemu_raise_exc(14, xemu_fault_err, xemu.rip);
            return 0;
          }
          xemu.gpr[7] += dir * sz;
        } else {
          uint64_t v = 0;
          if (xemu_vread(xemu.gpr[6], &v, sz) != 0) {
            xemu_raise_exc(14, xemu_fault_err, xemu.rip);
            return 0;
          }
          xemu_port_out(port, sz, v);
          xemu.gpr[6] += dir * sz;
        }
        if (!d->rep) break;
        xemu.gpr[1]--;
      }
      break;
    }
    case Ops_BSF: case Ops_BSR: {
      uint64_t v = read_op(d, 0, sz) & mask_of(sz);
      if (v == 0) {
        setf(F_ZF, 1);
      } else {
        setf(F_ZF, 0);
        int idx = 0;
        if (op == Ops_BSF) {
          while (!((v >> idx) & 1)) idx++;
        } else {
          idx = sz * 8 - 1;
          while (!((v >> idx) & 1)) idx--;
        }
        write_reg(d->a_idx, (uint64_t)idx, sz);
      }
      break;
    }
    case Ops_BT: case Ops_BTS: case Ops_BTR: case Ops_BTC: {
      /* With a memory destination the bit offset addresses the containing
         element: the effective address advances by (offset / operand_bits)
         elements and only the remainder indexes the bit. Linux relies on
         this when it sets bits >= 64 in a bitmap (e.g. system_vectors), so
         masking the offset without moving the address corrupts neighbours. */
      int bits = sz * 8;
      uint64_t raw = (d->b_kind == XK_IMM) ? d->imm : read_reg(d->b_idx, sz);
      int is_mem = (d->a_kind == XK_MEM);
      uint64_t ea = 0;
      uint64_t off = raw & (uint64_t)(bits - 1);
      if (is_mem) {
        /* Bochs (cpu/bit64.cc) masks first and then divides, so the element
           displacement keeps the sign of the (possibly sign-extended imm8)
           offset: an offset of -56 is element -1, bit 8 -- not element 0. */
        int64_t disp = (int64_t)(raw & ~((uint64_t)bits - 1));
        ea = xemu_ea(d) + (uint64_t)(disp / bits) * (uint64_t)sz;
      }
      uint64_t a = is_mem ? ld(ea, sz) : read_op(d, 1, sz);
      setf(F_CF, (a >> off) & 1);
      if (op != Ops_BT) {
        uint64_t m = 1ull << off;
        if (op == Ops_BTS) a |= m;
        else if (op == Ops_BTR) a &= ~m;
        else a ^= m;
        if (is_mem) st(ea, a, sz); else write_op(d, 1, a, sz);
      }
      break;
    }
    case Ops_CMPXCHG: {
      uint64_t src = read_op(d, 0, sz);
      uint64_t dst = read_op(d, 1, sz);
      uint64_t acc = read_reg(0, sz);
      uint64_t r = (acc - dst) & mask_of(sz);
      sub_flags(acc, dst, r, sz, 0);
      set_szp(r, sz);
      if (dst == acc) {
        setf(F_ZF, 1);
        write_op(d, 1, src, sz);
      } else {
        setf(F_ZF, 0);
        if (d->a_kind == XK_GPR && sz == 4) write_reg(d->a_idx, dst, sz);
        write_reg(0, dst, sz);
      }
      break;
    }
    case Ops_CMPXCHG8B: {
      int n = d->memsz ? d->memsz : 8;
      uint8_t buf[16];
      if (xemu_vread(xemu_ea(d), buf, n) != 0) {
        if (!xemu_fault_pending) { xemu_raise_exc(14, xemu_fault_err, xemu.rip); xemu_fault_pending = 1; }
        break;
      }
      uint64_t lo = 0, hi = 0;
      for (int i = 0; i < 8; i++) lo |= (uint64_t)buf[i] << (8 * i);
      for (int i = 8; i < 16; i++) hi |= (uint64_t)buf[i] << (8 * (i - 8));
      if (n == 8) {
        uint64_t cmp = ((xemu.gpr[2] & 0xffffffff) << 32) | (xemu.gpr[0] & 0xffffffff);
        if (lo == cmp) {
          setf(F_ZF, 1);
          uint64_t nv = ((xemu.gpr[1] & 0xffffffff) << 32) | (xemu.gpr[3] & 0xffffffff);
          xemu_vwrite(xemu_ea(d), &nv, 8);
        } else {
          setf(F_ZF, 0);
          xemu.gpr[0] = lo & 0xffffffff;
          xemu.gpr[2] = lo >> 32;
        }
      } else {
        if (lo == xemu.gpr[0] && hi == xemu.gpr[2]) {
          setf(F_ZF, 1);
          uint64_t nlo = xemu.gpr[3], nhi = xemu.gpr[1];
          uint8_t out[16];
          for (int i = 0; i < 8; i++) out[i] = (uint8_t)(nlo >> (8 * i));
          for (int i = 0; i < 8; i++) out[8 + i] = (uint8_t)(nhi >> (8 * i));
          xemu_vwrite(xemu_ea(d), out, 16);
        } else {
          setf(F_ZF, 0);
          xemu.gpr[0] = lo; xemu.gpr[2] = hi;
        }
      }
      break;
    }
    case Ops_MOV:
      if (d->a_kind == XK_XMM || d->b_kind == XK_XMM) return -1;
      write_op(d, 1, read_op(d, 0, d->memsz ? d->memsz : sz), sz);
      break;
    case Ops_MOVZX: case Ops_MOVSX: case Ops_MOVSXD: {
      int ssz = d->memsz;
      uint64_t v = read_op(d, 0, ssz);
      if (op == Ops_MOVZX) v &= mask_of(ssz);
      else {
        uint64_t m = mask_of(ssz);
        if (v & sign_of(ssz)) v |= ~m;
      }
      write_reg(d->a_idx, v, sz);
      break;
    }
    case Ops_MOV_SEG: {
      uint64_t *segs[6] = {&xemu.es, &xemu.cs, &xemu.ss, &xemu.ds, &xemu.fs, &xemu.gs};
      int si = d->a_idx;
      if (si < 0 || si > 5) break;
      if (d->cond == 0xff) break;
      if (d->dir == 0) {
        /* 8C: r/m <- Sreg. Memory form stores 16 bits; a register form
           zero-extends into 32 bits (REX.W writes all 64). */
        uint64_t v = *segs[si] & 0xffff;
        if (d->b_kind == XK_GPR) write_reg(d->b_idx, v, sz);
        else if (d->b_kind == XK_MEM) st(xemu_ea(d), v, 2);
      } else {
        if (si == 1) break;           /* cannot load CS from r/m */
        uint64_t v = read_op(d, 1, 2);
        *segs[si] = v & 0xffff;
      }
      break;
    }
    case Ops_LEA:
      write_reg(d->a_idx, xemu_ea(d), sz);
      break;
    case Ops_ADD: case Ops_ADC: case Ops_SUB: case Ops_SBB: case Ops_AND:
    case Ops_OR: case Ops_XOR: case Ops_CMP: case Ops_TEST: {
      uint64_t a = read_op(d, 1, sz), b = read_op(d, 0, sz);
      uint64_t r = alu_op(op, a, b, sz);
      if (op != Ops_CMP && op != Ops_TEST) write_op(d, 1, r, sz);
      break;
    }
    case Ops_INC: case Ops_DEC: {
      uint64_t a = read_op(d, 1, sz);
      uint64_t r = (op == Ops_INC) ? (a + 1) : (a - 1);
      int cf = getf(F_CF);
      if (op == Ops_INC) {
        setf(F_AF, (a & 0xf) == 0xf);
        setf(F_OF, a == (sign_of(sz) - 1));
      } else {
        setf(F_AF, (a & 0xf) == 0);
        setf(F_OF, a == sign_of(sz));
      }
      setf(F_CF, cf);
      set_szp(r, sz);
      write_op(d, 1, r, sz);
      break;
    }
    case Ops_NEG: {
      uint64_t a = read_op(d, 1, sz);
      uint64_t r = (0 - a) & mask_of(sz);
      setf(F_CF, a != 0);
      setf(F_OF, a == sign_of(sz));
      setf(F_AF, (a & 0xf) != 0);
      set_szp(r, sz);
      write_op(d, 1, r, sz);
      break;
    }
    case Ops_NOT: {
      uint64_t a = read_op(d, 1, sz);
      write_op(d, 1, ~a & mask_of(sz), sz);
      break;
    }
    case Ops_SHL: case Ops_SHR: case Ops_SAR:
    case Ops_ROL: case Ops_ROR: case Ops_RCL: case Ops_RCR: {
      uint64_t a = read_op(d, 1, sz);
      uint64_t c = read_op(d, 0, 1);
      uint64_t r = shift_op(op, a, c, sz);
      if (op == Ops_ROL || op == Ops_ROR || op == Ops_RCL || op == Ops_RCR) {
        /* ZF/SF/PF/AF preserved by rotates */
      }
      write_op(d, 1, r, sz);
      break;
    }
    case Ops_MUL: case Ops_IMUL: {
      int bits = sz * 8;
      int64_t sx = (int64_t)(xemu.gpr[0] << (64 - bits)) >> (64 - bits);
      (void)sx;
      if (op == Ops_IMUL && d->b_kind != XK_NONE) {
        /* two/three-operand IMUL: dest = rm * (imm or dest) */
        int64_t src = (int64_t)(read_op(d, 0, sz) << (64 - bits)) >> (64 - bits);
        int64_t mult;
        if (d->imm_kind != 0) mult = (int64_t)(d->imm << (64 - bits)) >> (64 - bits);
        else mult = (int64_t)(read_reg(d->a_idx, sz) << (64 - bits)) >> (64 - bits);
        __int128 full = (__int128)src * mult;
        uint64_t r = (uint64_t)full & mask_of(sz);
        int fits = full == (__int128)((int64_t)(r << (64 - bits)) >> (64 - bits));
        setf(F_CF, !fits); setf(F_OF, !fits);
        write_reg(d->a_idx, r, sz);
        break;
      }
      /* one-operand MUL/IMUL: implicit RAX (or AL/AX/EAX) */
      uint64_t src = read_op(d, 1, sz);
      if (bits == 8) {
        uint64_t a = read_reg(0, 1);
        if (op == Ops_MUL) {
          uint16_t prod = (uint16_t)(uint8_t)a * (uint16_t)(uint8_t)src;
          write_reg(0, prod, 2);
          setf(F_CF, prod >> 8); setf(F_OF, prod >> 8);
        } else {
          int16_t prod = (int16_t)(int8_t)a * (int16_t)(int8_t)src;
          write_reg(0, (uint16_t)prod, 2);
          int fits = prod == (int16_t)(int8_t)prod;
          setf(F_CF, !fits); setf(F_OF, !fits);
        }
      } else {
        uint64_t a = read_reg(0, sz);
        if (op == Ops_MUL) {
          unsigned __int128 prod = (unsigned __int128)a * src;
          uint64_t lo = (uint64_t)prod & mask_of(sz);
          uint64_t hi = (uint64_t)(prod >> bits) & mask_of(sz);
          write_reg(0, lo, sz); write_reg(2, hi, sz);
          setf(F_CF, hi != 0); setf(F_OF, hi != 0);
        } else {
          int64_t av = (int64_t)(a << (64 - bits)) >> (64 - bits);
          int64_t bv = (int64_t)(src << (64 - bits)) >> (64 - bits);
          __int128 prod = (__int128)av * bv;
          uint64_t lo = (uint64_t)prod & mask_of(sz);
          uint64_t hi = (uint64_t)(prod >> bits) & mask_of(sz);
          write_reg(0, lo, sz); write_reg(2, hi, sz);
          int fits = prod == (__int128)((int64_t)(lo << (64 - bits)) >> (64 - bits));
          setf(F_CF, !fits); setf(F_OF, !fits);
        }
      }
      break;
    }
    case Ops_DIV: case Ops_IDIV: {
      int bits = sz * 8;
      uint64_t divisor = read_op(d, 1, sz); /* one-operand DIV/IDIV: source is a */
      if (divisor == 0) { fprintf(stderr, "[xemu] divide by zero\n"); return -1; }
      if (bits == 8) {
        uint16_t dividend = (uint16_t)read_reg(0, 2);
        if (op == Ops_DIV) {
          uint16_t q = dividend / (uint8_t)divisor, r = dividend % (uint8_t)divisor;
          if (q > 0xff) return -1;
          write_reg(0, q, 1); write_reg(0, r, 1);
        } else {
          int16_t dv = (int16_t)dividend; int8_t dr = (int8_t)divisor;
          int16_t q = dv / dr, r = dv % dr;
          if (q < -128 || q > 127) return -1;
          write_reg(0, (uint8_t)q, 1); write_reg(0, (uint8_t)r, 1);
        }
      } else {
        uint64_t lo = read_reg(0, sz), hi = read_reg(2, sz);
        if (op == Ops_DIV) {
          unsigned __int128 dividend = ((unsigned __int128)hi << bits) | lo;
          unsigned __int128 q = dividend / divisor, r = dividend % divisor;
          if ((q >> bits) != 0) return -1;
          write_reg(0, (uint64_t)q, sz); write_reg(2, (uint64_t)r, sz);
        } else {
          __int128 dividend = ((__int128)(int64_t)(hi << (64 - bits)) >> (64 - bits)) << bits;
          dividend |= lo;
          int64_t dr = (int64_t)(divisor << (64 - bits)) >> (64 - bits);
          __int128 q = dividend / dr, r = dividend % dr;
          int64_t qv = (int64_t)q;
          if (q != (__int128)qv) return -1;
          write_reg(0, (uint64_t)q, sz); write_reg(2, (uint64_t)r, sz);
        }
      }
      break;
    }
    case Ops_CDQ: {
      uint64_t a = read_reg(0, sz);
      uint64_t v = (a & sign_of(sz)) ? mask_of(sz) : 0;
      write_reg(2, v, sz);
      break;
    }
    case Ops_CBW: {
      int half = sz / 2;
      uint64_t v = read_reg(0, half);
      if (v & sign_of(half)) v |= ~mask_of(half);
      write_reg(0, v, sz);
      break;
    }
    case Ops_XCHG: {
      uint64_t a = read_op(d, 1, sz), b = read_op(d, 0, sz);
      write_op(d, 1, b, sz);
      write_op(d, 0, a, sz);
      break;
    }
    case Ops_XADD: {
      uint64_t a = read_op(d, 1, sz), b = read_op(d, 0, sz);
      uint64_t r = a + b;
      add_flags(a, b, r, sz, 0); set_szp(r, sz);
      write_op(d, 0, a, sz);
      write_op(d, 1, r, sz);
      break;
    }
    case Ops_SETCC:
      write_op(d, 1, cond_true(d->cond) ? 1 : 0, 1);
      break;
    case Ops_CMOVCC:
      if (cond_true(d->cond)) write_reg(d->a_idx, read_op(d, 0, sz), sz);
      break;
    case Ops_JMP:
      if (d->b_kind == XK_IMM) {
        xemu.rip = xemu.rip + d->len + (uint64_t)d->imm;
      } else {
        /* An indirect jump through an unmapped address raises #PF inside the
           read.  xemu_raise_exc() has already installed the handler in RIP;
           storing the 0 returned by the faulting load would overwrite it and
           fetch from linear address 0 in kernel mode. */
        uint64_t tgt = read_op(d, 1, 8);
        if (xemu_fault_pending) return 0;
        xemu.rip = tgt;
      }
      return 0;
    case Ops_JCC:
      if (cond_true(d->cond)) { xemu.rip += d->len + (uint64_t)d->imm; return 0; }
      break;
    case Ops_CALL: {
      uint64_t tgt = (d->b_kind == XK_IMM) ? (xemu.rip + d->len + (uint64_t)d->imm)
                                           : read_op(d, 1, 8);
      int psz = d->opsz == 2 ? 2 : 8;
      push_val(xemu.rip + d->len, psz);
      if (xemu_fault_pending) return 0;   /* keep the handler RIP */
      xemu.rip = tgt;
      return 0;
    }
    case Ops_RET: {
      int psz = d->opsz == 2 ? 2 : 8;
      uint64_t tgt = pop_val(psz);
      if (xemu_fault_pending) return 0;   /* keep the handler RIP */
      if (d->b_kind == XK_IMM) xemu.gpr[4] += d->imm;
      xemu.rip = tgt;
      return 0;
    }
    case Ops_PUSH: {
      int psz = sz == 2 ? 2 : 8;
      uint64_t v = (d->b_kind == XK_IMM) ? d->imm : read_op(d, 1, psz);
      push_val(v, psz);
      break;
    }
    case Ops_POP: {
      int psz = sz == 2 ? 2 : 8;
      uint64_t v = pop_val(psz);
      write_op(d, 1, v, psz);
      break;
    }
    case Ops_PUSHF: {
      int psz = sz == 2 ? 2 : 8;
      push_val(xemu.rflags, psz);
      break;
    }
    case Ops_POPF: {
      int psz = sz == 2 ? 2 : 8;
      uint64_t v = pop_val(psz);
      xemu.rflags = (xemu.rflags & ~0xfd5ull) | (v & 0xfd5ull);
      break;
    }
    case Ops_LAHF: {
      uint64_t ah = (uint64_t)(getf(F_SF) << 7 | getf(F_ZF) << 6 | getf(F_AF) << 4 |
                               getf(F_PF) << 2 | 1u << 1 | getf(F_CF));
      xemu.gpr[0] = (xemu.gpr[0] & ~0xff00ull) | (ah << 8);
      break;
    }
    case Ops_SAHF: {
      uint64_t ah = (xemu.gpr[0] >> 8) & 0xff;
      setf(F_SF, (ah >> 7) & 1); setf(F_ZF, (ah >> 6) & 1);
      setf(F_AF, (ah >> 4) & 1); setf(F_PF, (ah >> 2) & 1);
      setf(F_CF, ah & 1);
      break;
    }
    case Ops_CLC: setf(F_CF, 0); break;
    case Ops_STC: setf(F_CF, 1); break;
    case Ops_CMC: setf(F_CF, !getf(F_CF)); break;
    case Ops_CLD: setf(F_DF, 0); break;
    case Ops_STD: setf(F_DF, 1); break;
    case Ops_CLI: setf(F_IF, 0); break;
    case Ops_STI: setf(F_IF, 1); break;
    case Ops_LOOP: case Ops_LOOPNE: case Ops_LOOPE: {
      uint64_t c = (xemu.gpr[1] - 1) & 0xffffffffffffffffull;
      xemu.gpr[1] = c;
      int taken = (c != 0);
      if (op == Ops_LOOPNE) taken = taken && !getf(F_ZF);
      if (op == Ops_LOOPE) taken = taken && getf(F_ZF);
      if (taken) { xemu.rip += d->len + (uint64_t)d->imm; return 0; }
      break;
    }
    case Ops_JRCXZ:
      if (xemu.gpr[1] == 0) { xemu.rip += d->len + (uint64_t)d->imm; return 0; }
      break;
    case Ops_NOP: case Ops_ENDBR:
      break;
    case Ops_HLT:
      /* HLT parks the CPU until an interrupt arrives; it is not program
       * exit. Tests end by storing to the halt MMIO window instead, so keep
       * executing: with IF clear this only advances the RIP. */
      xemu.halted_cpu = 1;
      xemu.rip += d->len;
      return 0;
    case Ops_MOV_DR: {
      int dreg = (int)(d->imm & 7);
      if (d->cond == 1) xemu.dr[dreg] = xemu.gpr[d->a_idx];
      else write_reg(d->a_idx, xemu.dr[dreg], 8);
      break;
    }
    case Ops_MOV_CR: {
      /* 0F 20: r64 <- CRn (a_idx = destination GPR, cond = 0)
         0F 22: CRn <- r64 (a_idx = source GPR,      cond = 1) */
      int cr = (int)(d->imm & 0xff);
      int is_write = d->cond == 1;
      uint64_t v = xemu.gpr[d->a_idx];
      if (!is_write) {
        switch (cr) {
          case 0: v = xemu.cr0; break;
          case 2: v = xemu.cr2; break;
          case 3: v = xemu.cr3; break;
          case 4: v = xemu.cr4; break;
          default: v = 0; break;
        }
        write_reg(d->a_idx, v, 8);
      } else {
        switch (cr) {
          case 0: xemu.cr0 = v; break;
          case 2: xemu.cr2 = v; break;
          case 3:
            xemu.cr3 = v & ~0xfffull;
            /* A CR3 load invalidates the TLB even when the value is
               unchanged: Linux's flush_tlb_mm()/flush_tlb_local() reload
               CR3 with the same value to flush stale entries (e.g. after
               fork write-protects the parent's COW pages).  Flushing only
               when the value changes leaves those entries writable and the
               parent then corrupts pages shared with the child. */
            xemu_tlb_flush();
            break;
          case 4: xemu.cr4 = v; break;
        }
      }
      break;
    }
    case Ops_CLAC: case Ops_STAC: case Ops_WBINVD: case Ops_SERIALIZE:
    case Ops_EMMS: case Ops_FWAIT: case Ops_MONITOR: case Ops_MWAIT:
    case Ops_TILERELEASE: case Ops_LKGS: case Ops_PREFETCH: case Ops_UD1:
      break;   /* no architectural state change in this model */
    case Ops_SHLD: case Ops_SHRD: {
      /* A = destination r/m, B = source register, count = imm8 or CL.
         Unlike SHL/SHR the source feeds the vacated bits. */
      int bits = sz * 8;
      /* The count is masked to 5 bits (6 for 64-bit operands) before any
         other check, so an immediate of 79 on a qword operand means 15. */
      int cnt = (d->imm_kind == 1) ? (int)(d->imm & 0xff) : (int)(read_reg(1, 1) & 0xff);
      cnt &= (sz == 8) ? 63 : 31;
      uint64_t m = mask_of(sz);
      if (cnt == 0) break;
      uint64_t dst = read_op(d, 1, sz);
      uint64_t src = read_op(d, 0, sz);
      uint64_t r;
      int cf;
      if (op == Ops_SHLD) {
        r = ((dst << cnt) | (src >> (bits - cnt))) & m;
        cf = (int)((dst >> (bits - cnt)) & 1);
      } else {
        r = ((dst >> cnt) | (src << (bits - cnt))) & m;
        cf = (int)((dst >> (cnt - 1)) & 1);
      }
      int msb = (int)((r >> (bits - 1)) & 1);
      int msb2 = (int)((r >> (bits - 2)) & 1);
      setf(F_CF, cf);
      setf(F_OF, msb ^ msb2);
      set_szp(r, sz);
      write_op(d, 1, r, sz);
      break;
    }
    case Ops_POPCNT: {
      uint64_t v = read_op(d, 0, sz);
      int n = 0;
      for (int i = 0; i < sz * 8; i++) n += (int)((v >> i) & 1);
      write_reg(d->a_idx, (uint64_t)n, sz);
      setf(F_ZF, n == 0);
      setf(F_CF, 0); setf(F_OF, 0); setf(F_SF, 0); setf(F_AF, 0); setf(F_PF, 0);
      break;
    }
    case Ops_MOVNTI:
      write_op(d, 1, read_op(d, 0, sz), sz);
      break;
    case Ops_RDRAND: case Ops_RDSEED: {
      /* Deterministic pseudo-random stream derived from the TSC so that
         guest-visible behaviour stays reproducible. */
      xemu.tsc = xemu.tsc * 6364136223846793005ull + 1442695040888963407ull;
      uint64_t v = xemu.tsc ^ (xemu.rip * 0x9E3779B97F4A7C15ull);
      write_reg(d->a_idx, v, sz);
      setf(F_CF, 1);
      break;
    }
    case Ops_LAR:
      setf(F_ZF, 0);   /* no LDT: selector not usable via LAR */
      break;
    case Ops_XSETBV:
      break;
    case Ops_XSAVES: case Ops_XSAVEC: case Ops_XRSTORS:
      break;
    case Ops_RDFSBASE:
      write_reg(d->a_idx, xemu.fs_base, 8);
      break;
    case Ops_RDGSBASE:
      write_reg(d->a_idx, xemu.gs_base, 8);
      break;
    case Ops_WRFSBASE:
      xemu.fs_base = read_reg(d->a_idx, 8);
      break;
    case Ops_WRGSBASE:
      xemu.gs_base = read_reg(d->a_idx, 8);
      break;
    case Ops_SWAPGS: {
      uint64_t t = xemu.gs_base;
      xemu.gs_base = xemu.kernel_gs_base;
      xemu.kernel_gs_base = t;
      break;
    }
    case Ops_RDTSC: case Ops_RDTSCP:
      xemu.gpr[0] = (xemu.gpr[0] & ~0xffffffffull) | (xemu.tsc & 0xffffffffu);
      xemu.gpr[2] = (xemu.gpr[2] & ~0xffffffffull) | (xemu.tsc >> 32);
      if (op == Ops_RDTSCP) xemu.gpr[1] = (xemu.gpr[1] & ~0xffffffffull);
      break;
    case Ops_RDPMC:
      xemu.gpr[0] = 0; xemu.gpr[2] = 0;
      break;
    case Ops_XGETBV:
      xemu.gpr[0] = 3;  /* XCR0: x87 | SSE */
      xemu.gpr[2] = 0;
      break;
    case Ops_CLTS:
      xemu.cr0 &= ~8ull;
      break;
    case Ops_PAUSE:
      break;
    case Ops_BSWAP: {
      uint64_t v = read_reg(d->a_idx, sz);
      if (sz == 8) {
        uint64_t r = 0;
        for (int i = 0; i < 8; i++) r |= ((v >> (8 * i)) & 0xff) << (8 * (7 - i));
        write_reg(d->a_idx, r, 8);
      } else {
        uint32_t x = (uint32_t)v, r = 0;
        for (int i = 0; i < 4; i++) r |= ((x >> (8 * i)) & 0xff) << (8 * (3 - i));
        write_reg(d->a_idx, r, 4);
      }
      break;
    }
    case Ops_LLDT:
      /* No LDT is modelled; accept the selector. */
      break;
    case Ops_VERR: case Ops_VERW:
      setf(F_ZF, 1);   /* selector considered valid: flat model, no LDT */
      break;
    case Ops_SGDT: case Ops_SIDT: {
      /* Store the pseudo-descriptor: 16-bit limit followed by the 64-bit
         base. Linux issues SGDT with an 8(-byte) offset into a 16-byte
         buffer and only inspects the base, so the full form is written. */
      uint64_t base = (op == Ops_SGDT) ? xemu.gdtr_base : xemu.idtr_base;
      uint16_t limit = (op == Ops_SGDT) ? xemu.gdtr_limit : xemu.idtr_limit;
      uint64_t ea = xemu_ea(d);
      /* The register form does not exist architecturally; tolerate it. */
      if (d->a_kind != XK_MEM) { write_reg(d->a_idx, base, 8); break; }
      st(ea, (uint64_t)limit, 2);
      st(ea + 2, base, 8);
      break;
    }
    case Ops_SMSW: {
      uint64_t v = xemu.cr0 & 0xffff;
      if (d->a_kind == XK_GPR) write_reg(d->a_idx, v, 2);
      else st(xemu_ea(d), v, 2);
      break;
    }
    case Ops_LMSW: {
      uint64_t v = (d->a_kind == XK_GPR) ? read_reg(d->a_idx, 2) : ld(xemu_ea(d), 2);
      xemu.cr0 = (xemu.cr0 & ~0xfull) | (v & 0xf);
      break;
    }
    case Ops_SLDT:
      if (d->a_kind == XK_GPR) write_reg(d->a_idx, 0, 2);
      else st(xemu_ea(d), 0, 2);
      break;
    case Ops_STR:
      if (d->a_kind == XK_GPR) write_reg(d->a_idx, xemu.tr_sel, 2);
      else st(xemu_ea(d), xemu.tr_sel, 2);
      break;
    case Ops_LTR: {
      uint64_t sel = read_reg(d->a_idx, 2) & 0xffff;
      uint64_t base = 0, limit = 0; uint32_t access = 0;
      if (xemu_gdt_read(sel, &base, &limit, &access) != 0) return -1;
      xemu.tr_sel = (uint16_t)sel;
      xemu.tr_base = base;
      xemu.tr_limit = limit;
      break;
    }
    case Ops_LGDT:
    case Ops_LIDT: {
      uint8_t b[10];
      /* The operand is a virtual address: translate through the page tables. */
      if (xemu_vread(xemu_ea(d), b, 10) != 0) {
        xemu_raise_exc(14, 0, xemu.rip);
        return 0;
      }
      uint64_t base = 0;
      for (int i = 0; i < 8; i++) base |= (uint64_t)b[2 + i] << (8 * i);
      if (op == Ops_LGDT) {
        xemu.gdtr_limit = (uint16_t)(b[0] | (b[1] << 8));
        xemu.gdtr_base = base;
      } else {
        xemu.idtr_limit = (uint16_t)(b[0] | (b[1] << 8));
        xemu.idtr_base = base;
      }
      break;
    }
    case Ops_LEAVE:
      /* RSP = RBP; pop RBP (operand-size dependent, 64-bit in long mode). */
      xemu.gpr[4] = xemu.gpr[5];
      xemu.gpr[5] = pop_val(sz == 2 ? 2 : 8);
      break;
    case Ops_ENTER: {
      /* ENTER imm16, imm8: allocate a frame with `level` nesting. */
      int alloc = (int)(d->imm & 0xffff);
      int level = (int)((d->imm >> 16) & 0x1f);
      int psz = sz == 2 ? 2 : 8;
      push_val(xemu.gpr[5], psz);
      uint64_t frame = xemu.gpr[4];
      if (level > 0) {
        uint64_t rbp = xemu.gpr[5];
        for (int i = 1; i < level; i++) {
          rbp -= psz;
          uint64_t v = 0;
          xemu_vread(rbp, &v, (size_t)psz);
          push_val(v, psz);
        }
        push_val(frame, psz);
      }
      xemu.gpr[5] = frame;
      xemu.gpr[4] -= (uint64_t)alloc;
      break;
    }
    case Ops_RETF: {
      /* Far return: pop RIP, then CS. In 64-bit mode the operand size is
       * always 64 bits and no SS:RSP is popped. The kernel uses this to
       * switch from the 32-bit compat segment to the 64-bit code segment. */
      uint64_t new_rip = 0, new_cs = 0;
      /* The stack holds virtual addresses; translate through the page tables. */
      if (xemu_vread(xemu.gpr[4], &new_rip, 8) != 0) {
        xemu_raise_exc(14, 0, xemu.rip); return 0;
      }
      xemu.gpr[4] += 8;
      if (xemu_vread(xemu.gpr[4], &new_cs, 8) != 0) {
        xemu_raise_exc(14, 0, xemu.rip); return 0;
      }
      xemu.gpr[4] += 8;
      xemu.rip = new_rip;
      xemu.cs = new_cs & 0xffff;
      if (d->imm) xemu.gpr[4] += d->imm;
      return 0;      /* do not fall through to the rip += len epilogue */
    }
    case Ops_IRET: {
      /* The RTL IRET microcode always pops the 5-slot frame. */
      uint64_t vals[5];
      for (int i = 0; i < 5; i++) {
        if (xemu_vread(xemu.gpr[4], &vals[i], 8) != 0) {
          xemu.gpr[4] = inst_entry_rsp;
          xemu_raise_exc(14, xemu_fault_err, xemu.rip);
          return 0;
        }
        xemu.gpr[4] += 8;
      }
      int new_cpl = (int)(vals[1] & 3);
      xemu.rip = vals[0]; xemu.cs = vals[1]; xemu.rflags = vals[2] | 2;
      xemu.gpr[4] = vals[3]; xemu.ss = vals[4];
      xemu.cpl = new_cpl;
      return 0;
    }
    case Ops_CPUID: {
      uint32_t leaf = (uint32_t)xemu.gpr[0];
      uint32_t a = 0, b = 0, c = 0, d = 0;
      /* Leaf 0: the product vendor string from docs/PRD.md 5.7. It is
         "OpenX86-Core" (12 bytes), returned as EBX/EDX/ECX. */
      if (leaf == 0) { a = 1; b = 0x6e65704f; c = 0x65726f43; d = 0x2d363858; }
      else if (leaf == 1) {
        /* docs/PRD.md 5.7: EDX = TSC, MSR, PAE, CX8, SEP, CMOV, SSE, SSE2,
           FXSR, CLFSH, with MMX = 0 and no APIC/HTT (single CPU, no APIC).
           FPU is deliberately *set* although the PRD table says 0: Linux
           panics with "Giving up, no FPU found" when CPUID.1:EDX.FPU is
           clear, and XEMU does implement the x87 subset (FXSAVE/FXRSTOR plus
           the arithmetic busybox and the kernel need), so claiming it is the
           honest choice. SEP is advertised because SYSENTER/SYSEXIT are
           implemented (see Ops_SYSENTER below); RDTSCP/LAHF_LM likewise. */
        a = 0x00000663;                               /* family 6, model 6, step 3 */
        b = 0;                                        /* brand index, CLFLUSH 8 */
        c = (1u << 13);                               /* CMPXCHG16B */
        d = (1u << 0) |                               /* FPU   (see above) */
            (1u << 4) |                               /* TSC   */
            (1u << 5) |                               /* MSR   */
            (1u << 6) |                               /* PAE   */
            (1u << 8) |                               /* CX8   */
            (1u << 11) |                              /* SEP   */
            (1u << 15) |                              /* CMOV  */
            (1u << 19) |                              /* CLFSH */
            (1u << 24) |                              /* FXSR  */
            (1u << 25) |                              /* SSE   */
            (1u << 26);                               /* SSE2  */
      }
      else if (leaf == 0x80000000u) { a = 0x80000008; }
      else if (leaf == 0x80000008u) {
        /* 48-bit virtual, 40-bit physical addresses (matches XEMU_MAXPHYADDR
           in mem.c, which drives the reserved-bit checks). */
        a = (48u << 8) | 40u;
      }
      else if (leaf == 0x80000001u) {
        c = 1;                                        /* LAHF_LM */
        d = (1u << 11) |                              /* SYSCALL */
            (1u << 20) |                              /* NX      */
            (1u << 27) |                              /* RDTSCP  */
            (1u << 29);                               /* LM      */
      }
      xemu.gpr[0] = a; xemu.gpr[3] = b; xemu.gpr[1] = c; xemu.gpr[2] = d;
      break;
    }
    case Ops_WRMSR: {
      uint32_t msr = (uint32_t)xemu.gpr[1];
      uint64_t v = (xemu.gpr[2] << 32) | (xemu.gpr[0] & 0xffffffffu);
      switch (msr) {
        case 0xc0000080: xemu.efer = v; break;
        case 0xc0000081: xemu.star = v; break;
        case 0xc0000082: xemu.lstar = v; break;
        case 0xc0000083: xemu.sfmask = v; break;

        case 0xc0000100: xemu.fs_base = v; break;
        case 0xc0000101: xemu.gs_base = v; break;
        case 0xc0000102: xemu.kernel_gs_base = v; break;
        case 0x00000277: xemu.pat = v; break;      /* IA32_PAT */
        case 0x000000fe: xemu.mtrr_def_type = v; break;
        case 0x00000174: xemu.sysenter_cs = v & 0xffff; break;
        case 0x00000175: xemu.sysenter_esp = v; break;
        case 0x00000176: xemu.sysenter_eip = v; break;
        default: break;
      }
      break;
    }
    case Ops_RDMSR: {
      uint32_t msr = (uint32_t)xemu.gpr[1];
      uint64_t v = 0;
      switch (msr) {
        case 0xc0000080: v = xemu.efer; break;
        case 0xc0000081: v = xemu.star; break;
        case 0xc0000082: v = xemu.lstar; break;
        case 0xc0000083: v = xemu.sfmask; break;
        case 0xc0000100: v = xemu.fs_base; break;
        case 0xc0000101: v = xemu.gs_base; break;
        case 0xc0000102: v = xemu.kernel_gs_base; break;
        case 0x00000277: v = xemu.pat; break;
        case 0x000000fe: v = xemu.mtrr_def_type; break;
        /* IA32_MTRRCAP and the MTRRphys{Base,Mask} pairs read back as zero:
           the memory model is a single cacheable RAM range, and CPUID does
           not advertise MTRR (docs/PRD.md 5.7), so no consumer can rely on
           per-range types. */
        case 0x000002ff: v = 0; break;
        case 0x00000174: v = xemu.sysenter_cs; break;
        case 0x00000175: v = xemu.sysenter_esp; break;
        case 0x00000176: v = xemu.sysenter_eip; break;
        case 0x00000010: v = xemu.tsc; break;      /* IA32_TSC */
        case 0x0000001b: v = 0; break;             /* IA32_APIC_BASE: no APIC */
        case 0x00000200: case 0x00000201: case 0x00000202: case 0x00000203:
        case 0x00000204: case 0x00000205: case 0x00000206: case 0x00000207:
        case 0x00000208: case 0x00000209: case 0x0000020a: case 0x0000020b:
        case 0x0000020c: case 0x0000020d: case 0x0000020e: case 0x0000020f:
          v = 0; break;                            /* MTRRphysBase/Mask */
        case 0x000001d9: v = 0; break;             /* IA32_DEBUGCTL */
        default: break;
      }
      xemu.gpr[0] = v & 0xffffffffu;
      xemu.gpr[2] = v >> 32;
      break;
    }
    case Ops_SYSENTER: {
      /* Intel SDM SYSENTER: CS = IA32_SYSENTER_CS, SS = CS+8, CPL 0,
         RIP = IA32_SYSENTER_EIP, RSP = IA32_SYSENTER_ESP, IF = 0. Segment
         bases are 0 (XEMU models flat segments only). */
      uint32_t cs = (uint32_t)(xemu.sysenter_cs & 0xffff);
      if (!(xemu.cr4 & 0x800)) {              /* CR4.SEP */
        xemu_raise_exc(6, 0, xemu.rip);       /* #UD */
        return 0;
      }
      if (!cs) return -1;                     /* #GP(0) unmodelled */
      xemu.cs = cs;
      xemu.ss = cs + 8;
      xemu.cpl = 0;
      xemu.rip = xemu.sysenter_eip;
      xemu.gpr[4] = xemu.sysenter_esp;
      xemu.rflags &= ~((1ull << 9) | (1ull << 8) | (1ull << 17)); /* IF,TF,VM */
      return 0;
    }
    case Ops_SYSEXIT: {
      /* Intel SDM SYSEXIT (64-bit): CS = IA32_SYSENTER_CS+16, SS = CS+8,
         CPL 3, RIP = RDX, RSP = RCX. */
      uint32_t cs = (uint32_t)(xemu.sysenter_cs & 0xffff);
      if (!(xemu.cr4 & 0x800)) {              /* CR4.SEP */
        xemu_raise_exc(6, 0, xemu.rip);
        return 0;
      }
      if (!cs || xemu.cpl != 0) return -1;    /* #GP(0) unmodelled */
      cs += 16;
      xemu.cs = cs | 3;
      xemu.ss = (cs + 8) | 3;
      xemu.cpl = 3;
      xemu.rip = xemu.gpr[2];                 /* RDX */
      xemu.gpr[4] = xemu.gpr[1];              /* RCX */
      return 0;
    }
    case Ops_SYSCALL:
      xemu_do_syscall(xemu.rip + d->len);
      return 0;
    case Ops_SYSRET:
      xemu_do_sysret();
      return 0;
    case Ops_INVLPG:
      xemu_tlb_invlpg(xemu_ea(d));
      break;
    case Ops_UD2:
      xemu_raise_exc(6, 0, xemu.rip);
      return 0;
    case Ops_INT3:
      xemu_raise_exc(3, 0, xemu.rip + d->len);
      return 0;
    case Ops_INT: {
      uint64_t vec = d->imm & 0xff;
      /* DPL check: INT n is only allowed if gate DPL >= CPL. */
      if (vec < 32) {
        uint8_t gate[16];
        /* The IDT is supervisor memory; the DPL probe must not fault when
           it is reached from CPL3 (U/S is enforced by the pager now). */
        int saved_cpl = xemu.cpl;
        xemu.cpl = 0;
        int gate_ok = xemu_vread(xemu.idtr_base + vec * 16, gate, 16) == 0;
        xemu.cpl = saved_cpl;
        if (gate_ok) {
          uint64_t low = 0;
          for (int i = 0; i < 8; i++) low |= (uint64_t)gate[i] << (8 * i);
          int dpl = (int)((low >> 45) & 3);
          if (xemu.cpl > dpl) {
            xemu_raise_exc(13, (uint64_t)(vec << 3) | 2, xemu.rip + d->len);
            return 0;
          }
        }
      }
      xemu_raise_exc(vec, 0, xemu.rip + d->len);
      return 0;
    }
    default:
      fprintf(stderr, "[xemu] unsupported op=%u at rip=0x%llx\n", op,
              (unsigned long long)xemu.rip);
      return -1;
  }
  if (xemu_fault_pending) { xemu_fault_pending = 0; xemu_after_inst(); return 0; }
  if (xemu_exc_delivered) {
    /* RIP already points into the handler: do not add the instruction
     * length, and do not tick devices (the handler runs next). */
    xemu_exc_delivered = 0;
    return 0;
  }
  xemu.rip += d->len;
  xemu_after_inst();
  if (xemu.irq_pending) {
    int vec = xemu.irq_pending;
    if (xemu.rflags & (1ull << 9)) {
      xemu.irq_pending = 0;
      xemu_raise_exc((uint64_t)vec, 0, xemu.rip);
    }
  }
  return 0;
}

void xemu_do_syscall(uint64_t next_rip) {
  xemu.gpr[1] = next_rip;
  xemu.gpr[11] = xemu.rflags;
  xemu.rflags &= ~xemu.sfmask;
  xemu.rip = xemu.lstar;
  /* STAR[47:32] holds the kernel CS selector. */
  uint32_t cs = (uint32_t)((xemu.star >> 32) & 0xffff);
  if (cs) { xemu.cs = cs; xemu.ss = (cs + 8) | 0; xemu.cpl = 0; }
}

void xemu_do_sysret(void) {
  xemu.rip = xemu.gpr[1];
  xemu.rflags = xemu.gpr[11] | 2;
  /* SYSRET returns to CPL3 with CS = STAR[63:48]+16, SS = CS+8. */
  uint32_t base = (uint32_t)((xemu.star >> 48) & 0xffff);
  if (base) {
    uint32_t cs = (base + 16) | 3;
    xemu.cs = cs;
    xemu.ss = (cs + 8) | 3;
    xemu.cpl = 3;
  }
}

/* Per-instruction device tick: UART input polling, PIT, and PIC delivery. */
void xemu_after_inst(void) {
  xemu_timer_tick();
  xemu_pit_tick();
  xemu_uart_poll();
  if (xemu.rflags & (1ull << 9)) {
    int vec = xemu_pic_ack();
    if (vec >= 0) xemu_raise_exc((uint64_t)vec, 0, xemu.rip);
  }
}
