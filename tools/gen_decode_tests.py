#!/usr/bin/env python3
"""Generate x86-64 decode test vectors.

Each instruction is assembled with llvm-mc and appended to a flat image.  The
vectors file records absolute PC, instruction length and a readable name.  The
Verilator decoder testbench feeds a 16-byte window starting at each PC into the
RTL decoder and compares the decoded length / legal flag with llvm-mc.
"""

import os
import re
import subprocess
import sys

BASE = 0x400000
LLVM_MC = os.environ.get("LLVM_MC", "llvm-mc")

CASES = []


def asm(text, tag=None):
    CASES.append(("asm", text, tag or text))


def raw(bs, tag):
    CASES.append(("raw", bs, tag))


def add_alu():
    sizes = [("b", "%al", "%bl", "$0x11"), ("w", "%ax", "%bx", "$0x1122"),
             ("l", "%eax", "%ebx", "$0x11223344"), ("q", "%rax", "%rbx", "$0x11223344")]
    for op in ["add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"]:
        for sfx, r, r2, imm in sizes:
            asm(f"{op}{sfx} {r2}, {r}")
            asm(f"{op}{sfx} 8(%rbx), {r}")
            asm(f"{op}{sfx} {r2}, 8(%rbx)")
            asm(f"{op}{sfx} {imm}, {r}")
            asm(f"{op}{sfx} {imm}, 8(%rbx)")
            if sfx in ("l", "q"):
                asm(f"{op}{sfx} $0x7f, {r}")        # imm8 sign-extended form (83 /x)
                asm(f"{op}{sfx} $-1, 8(%rbx)")


def add_mov():
    sizes = [("b", "%al", "%bl", "$0x11"), ("w", "%ax", "%bx", "$0x1122"),
             ("l", "%eax", "%ebx", "$0x11223344"), ("q", "%rax", "%rbx", "$0x1122334455667788")]
    for sfx, r, r2, imm in sizes:
        asm(f"mov{sfx} {r2}, {r}")
        asm(f"mov{sfx} 8(%rbx), {r}")
        asm(f"mov{sfx} {r2}, 8(%rbx)")
        asm(f"mov{sfx} $0x11, {r}")
        asm(f"mov{sfx} $0x11, 8(%rbx)")
        if sfx != "b":
            asm(f"mov{sfx} {imm}, {r}")
        else:
            asm(f"mov{sfx} $0x7f, %al")

    # Addressing modes
    for mem, tag in [
        ("(%rax)", "base"),
        ("0x7f(%rax)", "disp8"),
        ("-0x80(%rax)", "disp8_neg"),
        ("0x12345678(%rax)", "disp32"),
        ("(%rax,%rcx,1)", "sib1"),
        ("8(%rax,%rcx,2)", "sib2"),
        ("0x100(%rax,%rcx,4)", "sib4"),
        ("0x12345678(,%rcx,8)", "sib_nobase"),
        ("0x12345678(%rip)", "riprel"),
        ("(%r12)", "r12_base"),
        ("(%r13)", "r13_base_disp8"),
        ("8(%r13)", "r13_disp8"),
        ("0x12345678(%r13)", "r13_disp32"),
        ("(%r8,%r9,8)", "rex_sib"),
        ("%fs:0x10(%rax)", "fs_seg"),
        ("%gs:8(%rbx)", "gs_seg"),
    ]:
        asm(f"movq {mem}, %rbx", f"movq_{mem}_{tag}")
        asm(f"movq %rax, {mem}", f"movq_store_{tag}")

    # movzx / movsx / movsxd
    for s in ["movzbl %al, %eax", "movzbl %al, %ecx", "movzbq %al, %rax",
              "movzwl %ax, %eax", "movzwq %ax, %rax",
              "movsbl %al, %eax", "movsbq %al, %rax", "movswl %ax, %eax",
              "movswq %ax, %rax", "movslq %eax, %rax"]:
        asm(s)
    asm("movzbl 8(%rbx), %eax")
    asm("movslq 4(%rbx), %rax")

    asm("leaq 8(%rbx,%rcx,4), %rax")
    asm("leal 0x12345678(%rip), %eax")
    asm("leaq (%r12,%r13,2), %r14")

    asm("xchgb %al, %bl")
    asm("xchgq %rax, %rbx")
    asm("xchgq %rax, 8(%rbx)")


def add_unary_shift():
    for op in ["inc", "dec", "neg", "not"]:
        for sfx, r in [("b", "%al"), ("w", "%ax"), ("l", "%eax"), ("q", "%rax")]:
            asm(f"{op}{sfx} {r}")
            asm(f"{op}{sfx} 8(%rbx)")
    for op in ["shl", "shr", "sar", "rol", "ror", "rcl", "rcr"]:
        for sfx, r in [("b", "%al"), ("w", "%ax"), ("l", "%eax"), ("q", "%rax")]:
            asm(f"{op}{sfx} $1, {r}")
            asm(f"{op}{sfx} $3, {r}")
            asm(f"{op}{sfx} %cl, {r}")
            asm(f"{op}{sfx} $3, 8(%rbx)")


def add_mul_div():
    for s in ["imulq %rbx", "imulq %rbx, %rax", "imulq $5, %rbx, %rax",
              "imulq $0x11223344, 8(%rbx), %rax",
              "imull %ebx, %eax", "imulw %bx, %ax", "imulb %bl",
              "mulq %rbx", "mull %ebx", "mulb %bl",
              "divq %rbx", "divl %ebx", "divb %bl",
              "idivq %rbx", "idivl %ebx"]:
        asm(s)
    for s in ["cbtw", "cwtl", "cltq", "cwtd", "cltd", "cqto"]:
        asm(s)


def add_control():
    # Raw encodings: relative targets are placeholders.  Length is what matters.
    raw([0xeb, 0x00], "jmp_rel8")
    raw([0xe9, 0x00, 0x00, 0x00, 0x00], "jmp_rel32")
    for cc in range(16):
        raw([0x70 + cc, 0x00], f"jcc_rel8_{cc:02x}")
        raw([0x0f, 0x80 + cc, 0x00, 0x00, 0x00, 0x00], f"jcc_rel32_{cc:02x}")
    raw([0xe8, 0x00, 0x00, 0x00, 0x00], "call_rel32")
    raw([0xc3], "ret")
    raw([0xc2, 0x10, 0x00], "ret_imm16")
    for op in [0xe0, 0xe1, 0xe2, 0xe3]:
        raw([op, 0x00], f"loop_{op:02x}")

    asm("jmp *%rax")
    asm("call *%rbx")
    asm("jmp *8(%rax)")
    asm("call *8(%rax)")
    asm("pushq %rax")
    asm("pushq %r15")
    asm("pushq $0x11")
    asm("pushq $0x11223344")
    asm("pushq 8(%rbx)")
    asm("popq %rbx")
    asm("popq %r15")
    asm("popq 8(%rbx)")

    for cc in ["e", "ne", "l", "ge", "le", "g", "b", "ae", "be", "a", "s", "ns", "o", "no", "p", "np"]:
        asm(f"set{cc} %al")
        asm(f"set{cc} 8(%rbx)")
    asm("cmove %rbx, %rax")
    asm("cmovne 8(%rbx), %rax")
    asm("cmovg %rbx, %rax")


def add_misc():
    for s in ["nop", "nopl 0(%rax)", "nopw 0(%rax)", "endbr64",
              "hlt", "syscall", "cpuid", "ud2", "int3", "int $0x80",
              "pushfq", "popfq", "lahf", "sahf",
              "clc", "stc", "cmc", "cld", "std", "cli", "sti",
              "movsb", "movsw", "movsl", "movsq",
              "stosb", "stosw", "stosl", "stosq",
              "lodsb", "lodsq", "scasb", "scasq", "cmpsb", "cmpsq",
              "rep movsb", "rep movsq", "rep stosq", "repne scasb",
              "lock addq $1, (%rax)", "lock xchgq %rax, (%rbx)",
              "testb $0x11, %al", "testl $0x11223344, %eax",
              "testq $0x11, %rax", "testq %rax, 8(%rbx)",
              "xaddq %rax, 8(%rbx)"]:
        asm(s)

    asm("movl (%ebx), %eax")       # 0x67 address-size override
    asm("movl 0x10(%ebx), %eax")


def add_sse():
    # moves
    for s in ["movaps %xmm0, %xmm1", "movaps 16(%rax), %xmm0", "movaps %xmm0, 16(%rax)",
              "movups %xmm1, %xmm2", "movups 16(%rax), %xmm2",
              "movdqa %xmm3, %xmm4", "movdqa 16(%rax), %xmm3", "movdqa %xmm3, 16(%rax)",
              "movdqu %xmm5, %xmm6", "movdqu 16(%rax), %xmm5",
              "movss %xmm1, %xmm2", "movss 8(%rax), %xmm0", "movss %xmm0, 8(%rax)",
              "movsd %xmm1, %xmm2", "movsd 8(%rax), %xmm0", "movsd %xmm0, 8(%rax)",
              "movd %eax, %xmm0", "movd %xmm0, %eax",
              "movq %rax, %xmm0", "movq %xmm0, %rax",
              "movq %xmm1, %xmm2", "movq 8(%rax), %xmm1"]:
        asm(s)

    bin_ops = ["addps", "addpd", "addss", "addsd",
               "subps", "subpd", "subss", "subsd",
               "mulps", "mulpd", "mulss", "mulsd",
               "divps", "divpd", "divss", "divsd",
               "minps", "minpd", "minss", "minsd",
               "maxps", "maxpd", "maxss", "maxsd",
               "sqrtps", "sqrtpd", "sqrtss", "sqrtsd"]
    for op in bin_ops:
        asm(f"{op} %xmm1, %xmm0")
        asm(f"{op} 16(%rax), %xmm0")
    for op in ["andps", "andpd", "andnps", "andnpd", "orps", "orpd", "xorps", "xorpd",
               "pand", "por", "pxor", "paddd", "psubd"]:
        asm(f"{op} %xmm1, %xmm0")
        asm(f"{op} 16(%rax), %xmm0")
    for op in ["comiss", "comisd", "ucomiss", "ucomisd"]:
        asm(f"{op} %xmm1, %xmm0")
        asm(f"{op} 8(%rax), %xmm0")

    for s in ["cvtsi2ss %eax, %xmm0", "cvtsi2ss %rax, %xmm0",
              "cvtsi2sd %eax, %xmm0", "cvtsi2sd %rax, %xmm0",
              "cvttss2si %xmm0, %eax", "cvttss2si %xmm0, %rax",
              "cvttsd2si %xmm0, %eax", "cvttsd2si %xmm0, %rax",
              "cvtss2sd %xmm0, %xmm1", "cvtsd2ss %xmm0, %xmm1",
              "cvtdq2ps %xmm0, %xmm1", "cvtps2dq %xmm0, %xmm1",
              "cvtdq2pd %xmm0, %xmm1", "cvtpd2dq %xmm0, %xmm1"]:
        asm(s)


def build():
    for fn in [add_alu, add_mov, add_unary_shift, add_mul_div,
               add_control, add_misc, add_sse]:
        fn()


def assemble_one(text):
    try:
        p = subprocess.run([LLVM_MC, "-triple=x86_64", "-show-encoding"],
                           input=text + "\n", capture_output=True, text=True, timeout=20)
    except FileNotFoundError:
        sys.exit("llvm-mc not found; set LLVM_MC")
    if p.returncode != 0:
        return None, p.stderr.strip()
    m = re.search(r"encoding: \[([^\]]+)\]", p.stdout)
    if not m:
        return None, p.stdout.strip()
    out = []
    for tok in m.group(1).split(","):
        tok = tok.strip()
        if not re.fullmatch(r"0x[0-9a-fA-F]{2}", tok):
            return None, f"unresolved fixup byte {tok!r}"
        out.append(int(tok, 16))
    if not out:
        return None, "empty encoding"
    return bytes(out), None


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else "build/decode"
    os.makedirs(outdir, exist_ok=True)
    build()

    image = bytearray()
    vectors = []
    errors = []
    for kind, payload, tag in CASES:
        if kind == "raw":
            bs = bytes(payload)
            asm_text = tag
        else:
            bs, err = assemble_one(payload)
            asm_text = payload
            if bs is None:
                errors.append((payload, err))
                continue
        if len(bs) > 15:
            errors.append((asm_text, f"length {len(bs)} > 15"))
            continue
        pc = BASE + len(image)
        vectors.append((pc, len(bs), asm_text.replace(" ", "_")))
        image.extend(bs)

    image.extend(b"\xcc" * 32)
    with open(os.path.join(outdir, "image.bin"), "wb") as f:
        f.write(image)
    with open(os.path.join(outdir, "vectors.txt"), "w") as f:
        for pc, ln, name in vectors:
            f.write(f"{pc:016x} {ln} {name}\n")

    print(f"vectors: {len(vectors)}, image: {len(image)} bytes, errors: {len(errors)}")
    for bad in errors[:30]:
        print("  SKIP:", bad[0][:80], "->", bad[1][:120])
    if errors and len(errors) > 30:
        print("  ...")


if __name__ == "__main__":
    main()
