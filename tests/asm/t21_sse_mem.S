# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    lea fa(%rip), %r8
    lea fb(%rip), %r9
    movdqa (%r8), %xmm0
    addps (%r9), %xmm0
    mulps (%r9), %xmm0
    movdqa %xmm0, -16(%rsp)

    movsd (%r8), %xmm1
    addsd (%r9), %xmm1
    divsd (%r9), %xmm1
    movsd %xmm1, -24(%rsp)

    sqrtss (%r8), %xmm2
    movd %xmm2, %eax
    movq %rax, -32(%rsp)

    movq $0xf0000000, %r15
    movq $0, (%r15)

.p2align 4
fa:
    .long 0x40800000
    .long 0x41100000
    .long 0x41800000
    .long 0x41c80000
fb:
    .long 0x40000000
    .long 0x40400000
    .long 0x41000000
    .long 0x41400000
