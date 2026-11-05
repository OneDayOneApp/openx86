# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp

    # signed 32-bit conversion round trip
    movl $-123456789, %eax
    cvtsi2sd %eax, %xmm0
    cvttsd2si %xmm0, %ecx
    movq %rcx, -8(%rsp)

    # signed 64-bit conversion round trip
    movq $-1234567890123, %rax
    cvtsi2sdq %rax, %xmm1
    cvttsd2siq %xmm1, %rdx
    movq %rdx, -16(%rsp)

    # negative arithmetic
    movq $0xc004000000000000, %rax     # -2.5
    movq %rax, %xmm2
    movq $0x3ff0000000000000, %rax     # 1.0
    movq %rax, %xmm3
    subsd %xmm3, %xmm2                 # -3.5
    divsd %xmm3, %xmm2                 # -3.5
    movq %xmm2, -24(%rsp)

    # scalar float sub/div
    movl $0x40a00000, %eax             # 5.0f
    movd %eax, %xmm4
    movl $0x40000000, %eax             # 2.0f
    movd %eax, %xmm5
    subss %xmm5, %xmm4                 # 3.0f
    divss %xmm5, %xmm4                 # 1.5f
    movd %xmm4, %ecx
    movq %rcx, -32(%rsp)

    # min/max scalar double
    movq $0xc004000000000000, %rax     # -2.5
    movq %rax, %xmm6
    movq $0x4000000000000000, %rax     # 2.0
    movq %rax, %xmm7
    minsd %xmm7, %xmm6
    movq %xmm6, -40(%rsp)
    movsd %xmm7, %xmm8
    maxsd %xmm6, %xmm8
    movq %xmm8, -48(%rsp)

    # f32 <-> f64 conversions
    movl $0x40490fdb, %eax             # pi (float)
    movd %eax, %xmm9
    cvtss2sd %xmm9, %xmm10
    cvtsd2ss %xmm10, %xmm11
    movd %xmm11, %edx
    movq %rdx, -56(%rsp)

    # packed double add/mul
    lea dbl(%rip), %r8
    movdqa (%r8), %xmm12
    movdqa 16(%r8), %xmm13
    addpd %xmm13, %xmm12
    mulpd %xmm13, %xmm12
    movdqa %xmm12, -80(%rsp)

    movq $0xf0000000, %r15
    movq $0, (%r15)

.p2align 4
dbl:
    .quad 0xbff8000000000000
    .quad 0x4004000000000000
    .quad 0x3ff0000000000000
    .quad 0xc000000000000000
