# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp

    # scalar double sqrt(2.0)
    movq $0x4000000000000000, %rax
    movq %rax, %xmm0
    sqrtsd %xmm0, %xmm1
    movq %xmm1, %r8

    # scalar float sqrt(4.0)
    movl $0x40800000, %eax
    movd %eax, %xmm2
    sqrtss %xmm2, %xmm3
    movd %xmm3, %r9d

    # packed float sqrt of [4.0, 9.0, 16.0, 25.0]
    lea pf(%rip), %r10
    movdqa (%r10), %xmm4
    sqrtps %xmm4, %xmm5
    movdqa %xmm5, -16(%rsp)

    # packed double sqrt of [4.0, 9.0]
    lea pd(%rip), %r10
    movdqa (%r10), %xmm6
    sqrtpd %xmm6, %xmm7
    movdqa %xmm7, -32(%rsp)

    movq %r8, -40(%rsp)
    movq %r9, -48(%rsp)
    movq $0xf0000000, %r15
    movq $0, (%r15)

.p2align 4
pf:
    .long 0x40800000
    .long 0x41100000
    .long 0x41800000
    .long 0x41c80000
pd:
    .quad 0x4010000000000000
    .quad 0x4022000000000000
