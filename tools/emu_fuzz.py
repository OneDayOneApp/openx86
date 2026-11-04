#!/usr/bin/env python3
"""Random-program fuzzer for the per-instruction emu difftest.

Generates straight-line x86-64 programs from the implemented subset, assembles
them, and runs `build/emu` so every retired instruction is compared against the
reference model.  Failing programs are kept under tests/fuzz_fail/.
"""

import os
import random
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import difftest  # noqa: E402

EMU = os.path.join(ROOT, "build", "emu")
FAILDIR = os.path.join(ROOT, "tests", "fuzz_fail")

GPR = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
       "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"]
GPR32 = ["eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi",
         "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d"]
GPR16 = ["ax", "cx", "dx", "bx", "sp", "bp", "si", "di",
         "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w"]
GPR8 = ["al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil",
        "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b"]
# Avoid rsp/rbp as general operands; stack pointers are exercised via push/pop.
GEN = [0, 1, 2, 3, 6, 7, 8, 9, 10, 11, 12, 13]  # r14 = mem base, r15 = halt
MEM_OFF = [0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120]


def mem_operand(rng, size):
    off = rng.choice(MEM_OFF)
    return f"{off}(%r14)"


def mov_imm(rng, out):
    r = rng.choice(GEN)
    k = rng.choice([1, 4, 8])
    if k == 1:
        out.append(f"movb ${rng.randrange(-128, 256)}, %{GPR8[r]}")
    elif k == 4:
        out.append(f"movl ${rng.randrange(0, 1 << 32)}, %{GPR32[r]}")
    else:
        out.append(f"movabsq ${rng.randrange(0, 1 << 63)}, %{GPR[r]}")


def alu_reg(rng, out):
    op = rng.choice(["addq", "subq", "andq", "orq", "xorq", "adcq", "sbbq",
                     "imulq"])
    a, b = rng.choice(GEN), rng.choice(GEN)
    out.append(f"{op} %{GPR[a]}, %{GPR[b]}")


def alu_imm(rng, out):
    op = rng.choice(["addq", "subq", "andq", "orq", "xorq"])
    b = rng.choice(GEN)
    out.append(f"{op} ${rng.randrange(-128, 128)}, %{GPR[b]}")


def unary(rng, out):
    op = rng.choice(["incq", "decq", "negq", "notq"])
    out.append(f"{op} %{GPR[rng.choice(GEN)]}")


def shift(rng, out):
    op = rng.choice(["shlq", "shrq", "sarq", "rolq", "rorq"])
    b = rng.choice(GEN)
    if rng.random() < 0.5:
        out.append(f"{op} ${rng.randrange(1, 8)}, %{GPR[b]}")
    else:
        out.append("movb $%d, %%cl" % rng.randrange(0, 16))
        out.append(f"{op} %cl, %{GPR[b]}")


def memory(rng, out):
    kind = rng.random()
    r = rng.choice(GEN)
    if kind < 0.35:
        out.append(f"movq %{GPR[r]}, {mem_operand(rng, 8)}")
    elif kind < 0.6:
        out.append(f"movq {mem_operand(rng, 8)}, %{GPR[r]}")
    elif kind < 0.75:
        out.append(f"movl %{GPR32[r]}, {mem_operand(rng, 4)}")
    elif kind < 0.88:
        out.append(f"movl {mem_operand(rng, 4)}, %{GPR32[r]}")
    else:
        out.append(f"movw %{GPR16[r]}, {mem_operand(rng, 2)}")


def stack(rng, out):
    if rng.random() < 0.5:
        out.append(f"pushq %{GPR[rng.choice(GEN)]}")
    else:
        out.append(f"popq %{GPR[rng.choice(GEN)]}")


def compare_setcc(rng, out):
    a, b = rng.choice(GEN), rng.choice(GEN)
    cc = rng.choice(["e", "ne", "l", "le", "g", "ge", "a", "ae", "b", "be", "s", "ns"])
    out.append(f"{rng.choice(['cmpq', 'testq'])} %{GPR[a]}, %{GPR[b]}")
    out.append(f"set{cc} %r8b")
    out.append("movzbq %r8b, %r8")


def sse(rng, out):
    x = rng.randrange(16)
    y = rng.randrange(16)
    kind = rng.random()
    if kind < 0.25:
        out.append(f"movq %{GPR[rng.choice(GEN)]}, %xmm{x}")
    elif kind < 0.45:
        out.append(f"movdqa %xmm{x}, %xmm{y}")
    elif kind < 0.65:
        out.append(f"{rng.choice(['paddd', 'psubd', 'pxor', 'pand', 'por'])} %xmm{x}, %xmm{y}")
    elif kind < 0.85:
        off = rng.choice([0, 16, 32, 48, 64, 80, 96, 112])
        out.append(f"movdqa %xmm{x}, {off}(%r14)")
    else:
        off = rng.choice([0, 16, 32, 48, 64, 80, 96, 112])
        out.append(f"movdqa {off}(%r14), %xmm{x}")


OPS = [(mov_imm, 3), (alu_reg, 4), (alu_imm, 3), (unary, 2),
       (shift, 2), (memory, 4), (stack, 1), (compare_setcc, 2), (sse, 2)]


def gen_program(rng, n):
    out = ["    movq $0x800000, %rsp",
           "    movq $0x600000, %r14"]
    # Preload a few registers so ALU ops have non-trivial inputs.
    for r in (0, 1, 2, 6, 7, 8, 9, 10):
        out.append(f"    movabsq ${rng.randrange(1 << 63)}, %{GPR[r]}")
    total = sum(w for _, w in OPS)
    for _ in range(n):
        pick = rng.randrange(total)
        for fn, w in OPS:
            if pick < w:
                fn(rng, out)
                break
            pick -= w
    out += ["    movabsq $0xf0000000, %r15",
            "    movq $0, (%r15)"]
    return "\n".join(out) + "\n"


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    start = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    os.makedirs(FAILDIR, exist_ok=True)
    work = os.path.join(ROOT, "build", "fuzz")
    os.makedirs(work, exist_ok=True)
    failures = []
    for seed in range(start, start + count):
        rng = random.Random(seed)
        src = os.path.join(work, f"fuzz_{seed}.S")
        with open(src, "w") as f:
            f.write(gen_program(rng, rng.randrange(20, 60)))
        _, binp = difftest.assemble(src, f"fuzz_{seed}")
        r = subprocess.run([EMU, "-i", binp], capture_output=True, text=True, timeout=120)
        if r.returncode != 0 or "instructions cross-checked" not in r.stderr:
            failures.append(seed)
            keep = os.path.join(FAILDIR, f"fuzz_{seed}.S")
            shutil.copyfile(src, keep)
            print(f"FAIL seed={seed} -> {keep}")
            print("\n".join(r.stderr.strip().splitlines()[-4:]))
        else:
            n = [l for l in r.stderr.splitlines() if "instructions cross-checked" in l][-1]
            print(f"PASS seed={seed}: {n.split()[1]} instructions")
    print(f"\nfuzz: {count - len(failures)} passed, {len(failures)} failed")
    if failures:
        print("failed seeds: " + ", ".join(map(str, failures)))
        sys.exit(1)


if __name__ == "__main__":
    main()
