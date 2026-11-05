# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $0x600000, %rbx
    leaq va(%rip), %rax
    movdqa (%rax), %xmm0
    leaq vb(%rip), %rcx
    movdqa (%rcx), %xmm1
    movdqa %xmm0, (%rbx)
    movdqa %xmm1, 16(%rbx)
    movdqa (%rbx), %xmm2
    addps 16(%rbx), %xmm2
    mulps 16(%rbx), %xmm2
    subps (%rbx), %xmm2
    minps 16(%rbx), %xmm2
    movdqa %xmm0, %xmm3
    maxps 16(%rbx), %xmm3
    sqrtss 16(%rbx), %xmm4
    movdqa %xmm1, %xmm5
    addsd 32(%rbx), %xmm5
    hlt

.p2align 4
va:
    .long 0x3f800000
    .long 0x40000000
    .long 0x40400000
    .long 0x40800000
vb:
    .long 0x3f000000
    .long 0x3e800000
    .long 0x3e000000
    .long 0x3d800000
vd:
    .quad 0x3ff0000000000000
    .quad 0x4000000000000000
