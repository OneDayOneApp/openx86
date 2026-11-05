# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp

    lea f32a(%rip), %r8
    lea f32b(%rip), %r9
    movdqa (%r8), %xmm0
    movdqa (%r9), %xmm1
    addps %xmm1, %xmm0
    mulps %xmm1, %xmm0
    movdqa %xmm0, -16(%rsp)

    lea f64a(%rip), %r8
    lea f64b(%rip), %r9
    movdqa (%r8), %xmm2
    movdqa (%r9), %xmm3
    addpd %xmm3, %xmm2
    mulpd %xmm3, %xmm2
    movdqa %xmm2, -32(%rsp)

    # scalar sub/div
    movsd 16(%r8), %xmm4
    movsd 16(%r9), %xmm5
    subsd %xmm5, %xmm4
    divsd %xmm5, %xmm4
    movsd %xmm4, -40(%rsp)

    # min/max
    movss 24(%r8), %xmm6
    movss 24(%r9), %xmm7
    minss %xmm7, %xmm6
    movss %xmm6, -48(%rsp)
    movss 24(%r8), %xmm8
    maxss %xmm7, %xmm8
    movss %xmm8, -52(%rsp)

    # conversions
    movl $-42, %eax
    cvtsi2sd %eax, %xmm9
    cvttsd2si %xmm9, %ecx
    movq %rcx, -64(%rsp)
    cvtss2sd %xmm7, %xmm10
    cvtsd2ss %xmm10, %xmm11
    movq %xmm10, -72(%rsp)
    movd %xmm11, %edx
    movq %rdx, -80(%rsp)

    movq $0xf0000000, %r15
    movq $0, (%r15)

.p2align 4
f32a:
    .long 0x3f800000
    .long 0x40000000
    .long 0x40400000
    .long 0x40800000
f32b:
    .long 0x3f000000
    .long 0x40000000
    .long 0x40400000
    .long 0x40000000
f64a:
    .quad 0x3ff8000000000000
    .quad 0x4004000000000000
    .quad 0x401e000000000000
    .quad 0x4000000000000000
f64b:
    .quad 0x4002000000000000
    .quad 0x4000000000000000
    .quad 0x4004000000000000
    .quad 0x3ff0000000000000
