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

static uint64_t read_reg(int idx, int sz) {
  uint64_t v = xemu.gpr[idx];
  if (sz >= 8) return v;
  return v & mask_of(sz);
}

static void write_reg(int idx, uint64_t val, int sz) {
  if (sz >= 8) { xemu.gpr[idx] = val; return; }
  if (sz == 4) { xemu.gpr[idx] = val & 0xffffffffull; return; }
  uint64_t m = mask_of(sz);
  xemu.gpr[idx] = (xemu.gpr[idx] & ~m) | (val & m);
}

static uint64_t ea(const XemuInst *d) {
  if (d->rip_rel) return xemu.rip + d->len + (uint64_t)d->disp;
  uint64_t a = 0;
  if (d->has_base) a += xemu.gpr[d->base];
  if (d->has_index) a += xemu.gpr[d->index] << d->scale;
  a += (uint64_t)d->disp;
  return a;
}

static uint64_t ld(uint64_t addr, int sz) {
  uint8_t b[8] = {0};
  if (xemu_vread(addr, b, sz) != 0) {
    if (!xemu_fault_pending) {
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
      xemu_raise_exc(14, xemu_fault_err, xemu.rip);
      xemu_fault_pending = 1;
    }
  }
}

static uint64_t read_op(const XemuInst *d, int is_a, int sz) {
  int kind = is_a ? d->a_kind : d->b_kind;
  int idx = is_a ? d->a_idx : d->b_idx;
  if (kind == XK_GPR) return read_reg(idx, sz);
  if (kind == XK_MEM) return ld(ea(d), sz);
  if (kind == XK_IMM) return d->imm & mask_of(sz);
  return 0;
}

static void write_op(const XemuInst *d, int is_a, uint64_t v, int sz) {
  int kind = is_a ? d->a_kind : d->b_kind;
  int idx = is_a ? d->a_idx : d->b_idx;
  if (kind == XK_GPR) write_reg(idx, v, sz);
  else if (kind == XK_MEM) st(ea(d), v, sz);
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
}
static uint64_t pop_val(int sz) {
  uint64_t v = ld(xemu.gpr[4], sz);
  xemu.gpr[4] += sz;
  return v;
}

int xemu_sse_exec(const XemuInst *d);

int xemu_exec(const XemuInst *d) {
  int sz = d->opsz;
  uint16_t op = d->op;
  if (xemu_fault_pending) { xemu_fault_pending = 0; return 0; }
  if (op >= Ops_MOVD && op <= Ops_CVTTPD2DQ) return xemu_sse_exec(d);
  switch (op) {
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
    case Ops_LEA:
      write_reg(d->a_idx, ea(d), sz);
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
        if (d->imm != 0) mult = (int64_t)(d->imm << (64 - bits)) >> (64 - bits);
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
      xemu.rip = (d->b_kind == XK_IMM) ? (xemu.rip + d->len + (uint64_t)d->imm)
                                       : read_op(d, 1, 8);
      return 0;
    case Ops_JCC:
      if (cond_true(d->cond)) { xemu.rip += d->len + (uint64_t)d->imm; return 0; }
      break;
    case Ops_CALL: {
      uint64_t tgt = (d->b_kind == XK_IMM) ? (xemu.rip + d->len + (uint64_t)d->imm)
                                           : read_op(d, 1, 8);
      int psz = d->opsz == 2 ? 2 : 8;
      push_val(xemu.rip + d->len, psz);
      xemu.rip = tgt;
      return 0;
    }
    case Ops_RET: {
      int psz = d->opsz == 2 ? 2 : 8;
      uint64_t tgt = pop_val(psz);
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
      xemu.halted = 1;
      xemu.rip += d->len;
      return 0;
    case Ops_MOV_CR: {
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
          case 3: xemu.cr3 = v & ~0xfffull; break;
          case 4: xemu.cr4 = v; break;
        }
      }
      break;
    }
    case Ops_LIDT: {
      uint8_t b[10];
      if (xemu_mem_read(ea(d), b, 10) != 0) return -1;
      xemu.idtr_limit = (uint16_t)(b[0] | (b[1] << 8));
      uint64_t base = 0;
      for (int i = 0; i < 8; i++) base |= (uint64_t)b[2 + i] << (8 * i);
      xemu.idtr_base = base;
      break;
    }
    case Ops_IRET: {
      uint64_t vals[5];
      for (int i = 0; i < 5; i++) {
        if (xemu_mem_read(xemu.gpr[4], &vals[i], 8) != 0) return -1;
        xemu.gpr[4] += 8;
      }
      xemu.rip = vals[0]; xemu.cs = vals[1]; xemu.rflags = vals[2] | 2;
      xemu.gpr[4] = vals[3]; xemu.ss = vals[4];
      return 0;
    }
    case Ops_CPUID: {
      uint32_t leaf = (uint32_t)xemu.gpr[0];
      uint32_t a = 0, b = 0, c = 0, d = 0;
      if (leaf == 0) { a = 1; b = 0x2d363858; c = 0x65726f43; d = 0x2d484d4b; }
      else if (leaf == 1) { a = 0x00000663; b = 0; c = 0x07008020; d = 0x07008970; }
      else if (leaf == 0x80000000u) { a = 0x80000001; }
      else if (leaf == 0x80000001u) { c = 1; d = 0x20100800; }
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
        default: break;
      }
      xemu.gpr[0] = v & 0xffffffffu;
      xemu.gpr[2] = v >> 32;
      break;
    }
    case Ops_SYSCALL:
      xemu.gpr[1] = xemu.rip + d->len;
      xemu.gpr[11] = xemu.rflags;
      xemu.rflags &= ~xemu.sfmask;
      xemu.rip = xemu.lstar;
      return 0;
    case Ops_SYSRET:
      xemu.rip = xemu.gpr[1];
      xemu.rflags = xemu.gpr[11];
      return 0;
    case Ops_INVLPG:
      break;
    case Ops_UD2:
      xemu_raise_exc(6, 0, xemu.rip);
      return 0;
    case Ops_INT3:
      xemu_raise_exc(3, 0, xemu.rip + d->len);
      return 0;
    case Ops_INT:
      xemu_raise_exc(d->imm & 0xff, 0, xemu.rip + d->len);
      return 0;
    default:
      fprintf(stderr, "[xemu] unsupported op=%u at rip=0x%llx\n", op,
              (unsigned long long)xemu.rip);
      return -1;
  }
  if (xemu_fault_pending) { xemu_fault_pending = 0; return 0; }
  xemu.rip += d->len;
  return 0;
}
