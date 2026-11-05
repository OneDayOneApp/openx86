#!/usr/bin/env python3
"""Per-instruction Unicorn vs XEMU differential tester (XEMU P1).

Each vector initializes identical GPR/XMM/RFLAGS/memory in Unicorn and XEMU,
executes exactly one instruction at 0x400000, and compares architectural state.
"""

import ctypes
import os
import random
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, ".home", "pylibs"))
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import *  # noqa: E402

LIB = os.path.join(ROOT, "build", "ref", "libxemu.so")
TO_REF, TO_DUT = 1, 0
BASE = 0x400000
DATA = 0x600000
STACK = 0x800000
ENTRY = BASE

GPRS = ["rax","rcx","rdx","rbx","rsi","rdi","r8","r9","r10","r11","r12","r13"]
XMMS = [f"xmm{i}" for i in range(16)]
MEM = "0x30(%r14)"

FLAG_CF, FLAG_PF, FLAG_AF, FLAG_ZF, FLAG_SF, FLAG_OF = 0,2,4,6,7,11
BASE_MASK = (1<<FLAG_CF)|(1<<FLAG_PF)|(1<<FLAG_AF)|(1<<FLAG_ZF)|(1<<FLAG_SF)|(1<<FLAG_OF)
LOGIC = {"and","or","xor","test"}


class DiffState(ctypes.Structure):
    _fields_ = [("gpr", ctypes.c_uint64 * 16),
                ("rip", ctypes.c_uint64),
                ("rflags", ctypes.c_uint64),
                ("xmm", (ctypes.c_uint64 * 2) * 16)]


def assemble(instrs):
    """Assemble a list of single instructions; return list of byte strings."""
    src = ".text\n" + "\n".join(instrs) + "\n"
    with tempfile.NamedTemporaryFile("w", suffix=".s", delete=False) as f:
        f.write(src)
        path = f.name
    try:
        out = subprocess.run(["llvm-mc", "-triple=x86_64", "-show-encoding", path],
                             capture_output=True, text=True, check=True).stdout
    finally:
        os.unlink(path)
    encs, cur = [], None
    for line in out.splitlines():
        if "encoding: [" in line:
            if cur is not None:
                encs.append(cur)
            hexs = line.split("encoding: [", 1)[1].split("]", 1)[0]
            cur = bytes(int(x.strip()[2:], 16) for x in hexs.split(",") if x.strip())
    if cur is not None:
        encs.append(cur)
    if len(encs) != len(instrs):
        raise RuntimeError(f"assembled {len(encs)} of {len(instrs)}")
    return encs


def rand_state(rng):
    st = DiffState()
    for i in range(16):
        st.gpr[i] = rng.getrandbits(64)
    st.gpr[4] = STACK            # rsp
    st.gpr[14] = DATA            # mem base
    st.gpr[15] = 0xF0000000      # unused
    st.rip = ENTRY
    st.rflags = 0x202 | (rng.getrandbits(8) << 0)
    for i in range(16):
        st.xmm[i][0] = rng.getrandbits(64)
        st.xmm[i][1] = rng.getrandbits(64)
    return st


def load_xemu():
    lib = ctypes.CDLL(LIB)
    lib.difftest_init.argtypes = [ctypes.c_int]
    lib.difftest_memcpy.argtypes = [ctypes.c_uint64, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
    lib.difftest_regcpy.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.difftest_exec.argtypes = [ctypes.c_uint64]
    return lib


def xemu_exec(lib, code, st, mem):
    lib.difftest_init(0)
    buf = ctypes.create_string_buffer(code, len(code))
    lib.difftest_memcpy(ENTRY, buf, len(code), TO_REF)
    mbuf = ctypes.create_string_buffer(mem, len(mem))
    lib.difftest_memcpy(DATA, mbuf, len(mem), TO_REF)
    lib.difftest_regcpy(ctypes.byref(st), TO_REF)
    lib.difftest_exec(1)
    out = DiffState()
    lib.difftest_regcpy(ctypes.byref(out), TO_DUT)
    got = ctypes.create_string_buffer(len(mem))
    lib.difftest_memcpy(DATA, got, len(mem), TO_DUT)
    return out, got.raw


def uc_run(code, st, mem):
    uc = Uc(UC_ARCH_X86, UC_MODE_64)
    uc.mem_map(0, 16*1024*1024)
    uc.mem_write(ENTRY, code)
    uc.mem_write(DATA, mem)
    regmap = {0:UC_X86_REG_RAX,1:UC_X86_REG_RCX,2:UC_X86_REG_RDX,3:UC_X86_REG_RBX,
              4:UC_X86_REG_RSP,5:UC_X86_REG_RBP,6:UC_X86_REG_RSI,7:UC_X86_REG_RDI,
              8:UC_X86_REG_R8,9:UC_X86_REG_R9,10:UC_X86_REG_R10,11:UC_X86_REG_R11,
              12:UC_X86_REG_R12,13:UC_X86_REG_R13,14:UC_X86_REG_R14,15:UC_X86_REG_R15}
    for i, reg in regmap.items():
        uc.reg_write(reg, st.gpr[i])
    uc.reg_write(UC_X86_REG_RIP, st.rip)
    uc.reg_write(UC_X86_REG_EFLAGS, st.rflags)
    for i in range(16):
        uc.reg_write(getattr(UC_X86_REG_XMM0, "value", 0) + 0 if False else getattr(__import__("unicorn.x86_const", fromlist=[f"UC_X86_REG_XMM{i}"]), f"UC_X86_REG_XMM{i}"),
                     (st.xmm[i][1] << 64) | st.xmm[i][0])
    end = ENTRY + len(code)
    def stop_at_end(uc_, addr, size, user):
        if addr >= end:
            uc_.emu_stop()
    uc.hook_add(UC_HOOK_CODE, stop_at_end)
    uc.emu_start(ENTRY, 0, 0, 0)
    out = DiffState()
    for i, reg in regmap.items():
        out.gpr[i] = uc.reg_read(reg)
    out.rip = uc.reg_read(UC_X86_REG_RIP)
    out.rflags = uc.reg_read(UC_X86_REG_EFLAGS)
    for i in range(16):
        v = uc.reg_read(getattr(__import__("unicorn.x86_const", fromlist=[f"UC_X86_REG_XMM{i}"]), f"UC_X86_REG_XMM{i}"))
        out.xmm[i][0] = v & ((1<<64)-1)
        out.xmm[i][1] = (v >> 64) & ((1<<64)-1)
    return out, uc.mem_read(DATA, len(mem))


def mnemo(asm):
    return asm.split()[0]

def base_mn(asm):
    import re
    m = mnemo(asm)
    if m in {"rep", "repe", "repne"}:
        m = asm.split()[1]
    if m in {"cmpxchg8b", "cmpxchg16b"}:
        return m
    return re.sub(r"[bwlq]$", "", m)


def flag_mask(asm):
    base = base_mn(asm)
    mask = BASE_MASK
    if base in {"shl","shr","sar","rol","ror","rcl","rcr","shld","shrd"}:
        mask &= ~((1<<FLAG_OF)|(1<<FLAG_AF))
    if base in {"mul","imul"}:
        mask &= ~((1<<FLAG_PF)|(1<<FLAG_AF)|(1<<FLAG_ZF)|(1<<FLAG_SF))
    if base in {"div","idiv"}:
        mask = 0
    if base in LOGIC:
        mask &= ~(1<<FLAG_AF)
    if base in {"bsf", "bsr"}:
        mask = (1 << FLAG_ZF)  # only ZF is defined
    if base in {"cmpxchg8b", "cmpxchg16b"}:
        mask = (1 << FLAG_ZF)
    return mask


def is_nan64(x):
    return (x & 0x7ff0000000000000) == 0x7ff0000000000000 and (x & 0xfffffffffffff) != 0

def is_nan32(x):
    x &= 0xffffffff
    return (x & 0x7f800000) == 0x7f800000 and (x & 0x7fffff) != 0

def lane_eq(a, b, is32=False):
    if a == b:
        return True
    if is32:
        return is_nan32(a) and is_nan32(b)
    return is_nan64(a) and is_nan64(b)

def check(asm, code, rng):
    st = rand_state(rng)
    mem = bytes(rng.getrandbits(8) for _ in range(0x400))
    b = base_mn(asm)
    if b in {"bt", "bts", "btr", "btc"}:
        # Unicorn does NOT advance the effective address for bit offsets past
        # the operand size (real hardware and Bochs do: see
        # cpu/bit64.cc BTS_EqGqM). Keep the oracle comparison inside the
        # offset range where both agree; the element-advance path is covered
        # by tests/system/t63_bt_mem.S with Bochs-verified semantics.
        st.gpr[1] = rng.randrange(0, 64)
    if ("*%rcx" in asm) or ("*0x30(%r14)" in asm):
        tgt = ENTRY + 0x100
        st.gpr[1] = tgt
        mem = bytearray(mem)
        mem[0x30:0x38] = tgt.to_bytes(8, "little")
        mem = bytes(mem)
    if b in {"shld", "shrd"} and "%cl" in asm:
        st.gpr[1] = rng.randrange(1, 16)   # defined range for every operand size
    if b in {"movs", "stos", "lods", "scas", "cmps"}:
        st.gpr[6] = DATA + 0x100
        st.gpr[7] = DATA + 0x200
        st.gpr[1] = rng.randrange(0, 5)
        if rng.randrange(2):
            st.rflags &= ~(1 << 10)   # DF=0
        else:
            st.rflags |= (1 << 10)
    lib = load_xemu()
    if os.environ.get("ISA_TRACE"):
        sys.stderr.write(f"[vector] {asm}  ({code.hex()})\n")
        sys.stderr.flush()
    xout, xmem = xemu_exec(lib, code, st, mem)
    uout, umem = uc_run(code, st, mem)
    if xout.rip != uout.rip:
        return f"rip {xout.rip:#x} != {uout.rip:#x}"
    for i in range(16):
        if xout.gpr[i] != uout.gpr[i]:
            return f"gpr[{i}] {xout.gpr[i]:#x} != {uout.gpr[i]:#x}"
    m = mnemo(asm)
    if m in {"movd","movq"}:
        for i in range(16):
            if (xout.xmm[i][0] & 0xffffffff) != (uout.xmm[i][0] & 0xffffffff):
                return f"xmm{i} low"
    else:
        m = mnemo(asm)
        scalar_ss = m in {"addss","subss","mulss","divss","minss","maxss","sqrtss","comiss","ucomiss"}
        scalar_sd = m in {"addsd","subsd","mulsd","divsd","minsd","maxsd","sqrtsd","comisd","ucomisd"}
        for i in range(16):
            x, u = xout.xmm[i], uout.xmm[i]
            if scalar_ss:
                ok = (x[0] >> 32) == (u[0] >> 32) and x[1] == u[1] and \
                     lane_eq(x[0] & 0xffffffff, u[0] & 0xffffffff, is32=True)
            elif scalar_sd:
                ok = x[1] == u[1] and lane_eq(x[0], u[0])
            else:
                ok = x[0] == u[0] and x[1] == u[1]
            if not ok:
                return f"xmm{i} {x[1]:016x}{x[0]:016x} != {u[1]:016x}{u[0]:016x}"
    mask = flag_mask(asm)
    if ((xout.rflags ^ uout.rflags) & mask):
        return f"flags {xout.rflags:#x} != {uout.rflags:#x} mask={mask:#x}"
    if xmem != umem:
        for i, (a, b) in enumerate(zip(xmem, umem)):
            if a != b:
                return f"mem[{i:#x}] {a:#x} != {b:#x}"
    return None


TEMPLATES = [
    "movq {r1}, {r2}", "movl {r1d}, {r2d}", "movw {r1w}, {r2w}", "movb {r1b}, {r2b}",
    "movabsq ${imm64}, {r1}",
    "addq {r1}, {r2}", "subq {r1}, {r2}", "andq {r1}, {r2}", "orq {r1}, {r2}",
    "xorq {r1}, {r2}", "adcq {r1}, {r2}", "sbbq {r1}, {r2}", "cmpq {r1}, {r2}",
    "testq {r1}, {r2}",
    "addq ${imm8}, {r1}", "subq ${imm8}, {r1}", "andq ${imm8}, {r1}",
    "incq {r1}", "decq {r1}", "negq {r1}", "notq {r1}",
    "shlq ${imm8}, {r1}", "shrq ${imm8}, {r1}", "sarq ${imm8}, {r1}",
    "rolq ${imm8}, {r1}", "rorq ${imm8}, {r1}",
    "shlq %cl, {r1}", "shrq %cl, {r1}", "sarq %cl, {r1}",
    "imulq {r1}, {r2}", "imulq ${imm8}, {r1}, {r2}",
    "movzbq {r1b}, {r2}", "movswq {r1w}, {r2}", "movslq {r1d}, {r2}",
    "leaq {mem}, {r2}",
    "movq {r1}, {mem}", "movq {mem}, {r2}",
    "addq {r1}, {mem}", "cmpq {r1}, {mem}",
    "movd {r1d}, %xmm0", "movd %xmm0, {r1d}", "movq {r1}, %xmm0", "movq %xmm0, {r1}",
    "movdqa %xmm1, %xmm0", "paddd %xmm1, %xmm0", "psubd %xmm1, %xmm0",
    "pand %xmm1, %xmm0", "por %xmm1, %xmm0", "pxor %xmm1, %xmm0",
    "paddb %xmm1, %xmm0", "paddw %xmm1, %xmm0", "paddq %xmm1, %xmm0",
    "psubb %xmm1, %xmm0", "psubw %xmm1, %xmm0", "psubq %xmm1, %xmm0",
    "pcmpeqb %xmm1, %xmm0", "pcmpeqw %xmm1, %xmm0", "pcmpeqd %xmm1, %xmm0",
    "pcmpgtb %xmm1, %xmm0", "pcmpgtw %xmm1, %xmm0", "pcmpgtd %xmm1, %xmm0",
    "pshufd $0x1b, %xmm1, %xmm0",
    "punpcklbw %xmm1, %xmm0", "punpcklwd %xmm1, %xmm0",
    "punpckldq %xmm1, %xmm0", "punpcklqdq %xmm1, %xmm0",
    "punpckhbw %xmm1, %xmm0", "punpckhwd %xmm1, %xmm0",
    "punpckhdq %xmm1, %xmm0", "punpckhqdq %xmm1, %xmm0",
    "pmovmskb %xmm1, {r1d}",
    "movntdq %xmm1, 0x30(%r14)",
    "jmp *%rcx", "call *%rcx", "jmp *0x30(%r14)", "call *0x30(%r14)",
    "addsd %xmm1, %xmm0", "subsd %xmm1, %xmm0", "mulsd %xmm1, %xmm0",
    "divsd %xmm1, %xmm0", "sqrtsd %xmm1, %xmm0", "minsd %xmm1, %xmm0", "maxsd %xmm1, %xmm0",
    "addss %xmm1, %xmm0", "mulss %xmm1, %xmm0", "sqrtss %xmm1, %xmm0",
    "comisd %xmm1, %xmm0", "ucomisd %xmm1, %xmm0",
    "cvtsi2sd {r1}, %xmm0", "cvttsd2si %xmm1, {r2}", "cvtsd2ss %xmm1, %xmm0",
    "cvtss2sd %xmm1, %xmm0",
    "movsb", "movsw", "movsl", "movsq",
    "stosb", "stosw", "stosl", "stosq",
    "lodsb", "lodsw", "lodsl", "lodsq",
    "cmpsb", "cmpsw", "cmpsl", "cmpsq",
    "scasb", "scasw", "scasl", "scasq",
    "bsfq {r1}, {r2}", "bsrq {r1}, {r2}", "bsfl {r1d}, {r2d}", "bsfw {r1w}, {r2w}",
    "btq %rcx, {r1}", "btsq %rcx, {r1}", "btrq %rcx, {r1}", "btcq %rcx, {r1}",
    "btq $13, {r1}", "btsq $13, {r1}", "btrq $13, {r1}", "btcq $13, {r1}",
    "cmpxchgq {r1}, {r2}", "cmpxchgl {r1d}, {r2d}",
    "cmpxchgq {r1}, 0x30(%r14)",
    "cmpxchg8b 0x30(%r14)", "cmpxchg16b 0x30(%r14)",
    "rep movsb", "rep movsq", "rep stosb", "rep stosq",
    "rep lodsb", "rep lodsq", "repe cmpsb", "repne scasb",
    "sete %cl", "setne %cl", "setl %cl", "setg %cl",
    "cmove {r1}, {r2}", "cmovne {r1}, {r2}",
    # Bit ops with a *memory* destination and offsets that cross elements.
    "btq %rcx, 0x30(%r14)", "btsq %rcx, 0x30(%r14)",
    "btrq %rcx, 0x30(%r14)", "btcq %rcx, 0x30(%r14)",
    "btl %ecx, 0x30(%r14)", "btsl %ecx, 0x30(%r14)",
    "btw %cx, 0x30(%r14)", "btsw %cx, 0x30(%r14)",
    "btsq $42, 0x30(%r14)", "btcq $62, 0x30(%r14)", "btrq $1, 0x30(%r14)",
    # Legacy high-byte registers (AH/CH/DH/BH), only valid without REX.
    "testb $0x10, %ah", "testb $0x10, %bh",
    "movb %ah, %cl", "movb %al, %ch", "movb %dh, %bl", "movb %bh, %dl",
    "addb %ah, %cl", "xorb %dh, %al", "cmpb %bh, %dl",
    "movzbl %ah, %eax", "movzbl %bh, %ecx", "movsbl %ch, %edx",
    "incb %ah", "negb %dh", "notb %bh",
    "shlb $3, %ah", "sarb $1, %dh",
    # Double-precision shifts, popcount, byte swaps, non-temporal stores.
    "shldq ${imm8}, {r1}, {r2}", "shrdq ${imm8}, {r1}, {r2}",
    "shldl ${imm8}, {r1d}, {r2d}", "shrdl ${imm8}, {r1d}, {r2d}",
    "shldw ${imm8}, {r1w}, {r2w}", "shrdw ${imm8}, {r1w}, {r2w}",
    "shldq %cl, {r1}, {r2}", "shrdq %cl, {r1}, {r2}",
    "popcntq {r1}, {r2}", "popcntl {r1d}, {r2d}", "popcntw {r1w}, {r2w}",
    "bswapq {r1}", "bswapl {r1d}",
    "movntiq {r1}, 0x30(%r14)", "movntil {r1d}, 0x30(%r14)",
]


def fill(asm, rng):
    r1 = rng.choice(GPRS); r2 = rng.choice([x for x in GPRS if x != r1])
    # SHLD/SHRD results are architecturally undefined once the (5-bit masked)
    # count exceeds the operand size, so keep the generated counts in a range
    # that is defined for 16, 32 and 64-bit forms.
    slice_shift = base_mn(asm) in {"shld", "shrd"}
    def reg32(r): return r.replace("r8","r8d").replace("r9","r9d").replace("r10","r10d").replace("r11","r11d").replace("r12","r12d").replace("r13","r13d").replace("rax","eax").replace("rcx","ecx").replace("rdx","edx").replace("rbx","ebx").replace("rsi","esi").replace("rdi","edi")
    def reg16(r): return r + "w" if r.startswith("r") and r[1:].isdigit() else ("a" if False else r.replace("rax","ax").replace("rcx","cx").replace("rdx","dx").replace("rbx","bx").replace("rsi","si").replace("rdi","di"))
    def reg8(r): return r + "b" if r.startswith("r") and r[1:].isdigit() else r.replace("rax","al").replace("rcx","cl").replace("rdx","dl").replace("rbx","bl").replace("rsi","sil").replace("rdi","dil")
    return asm.format(r1="%"+r1, r2="%"+r2,
                      r1d="%"+reg32(r1), r2d="%"+reg32(r2),
                      r1w="%"+reg16(r1), r2w="%"+reg16(r2),
                      r1b="%"+reg8(r1), r2b="%"+reg8(r2),
                      imm8=(rng.randrange(1, 16) if slice_shift else rng.randrange(-128, 128)),
                      imm64=rng.getrandbits(63),
                      mem=MEM)


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 10
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    rng = random.Random(seed)
    asms, codes = [], []
    for _ in range(count * len(TEMPLATES)):
        asm = fill(rng.choice(TEMPLATES), rng)
        asms.append(asm)
    codes = assemble(asms)
    fail = 0
    for asm, code in zip(asms, codes):
        err = check(asm, code, rng)
        if err:
            fail += 1
            if fail <= 30:
                print(f"FAIL {asm}: {err}")
    total = len(asms)
    print(f"isa diff: {total-fail}/{total} passed, {fail} failed")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
