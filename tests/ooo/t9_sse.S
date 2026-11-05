# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $0x1122334455667788, %rax
    movq %rax, %xmm0
    movd %eax, %xmm1
    movdqa %xmm0, %xmm2
    paddd %xmm1, %xmm2
    pand %xmm0, %xmm2
    por %xmm1, %xmm2
    pxor %xmm2, %xmm3
    movq %xmm2, %rbx
    movd %xmm2, %ecx
    movq $0x4000000000000000, %rdx
    movq %rdx, %xmm4
    addsd %xmm4, %xmm4
    sqrtsd %xmm4, %xmm5
    cvtsi2sd %rax, %xmm6
    cvttsd2si %xmm5, %rdi
    cvtdq2ps %xmm1, %xmm7
    cvtps2dq %xmm7, %xmm8
    comisd %xmm5, %xmm4
    seta %r8b
    movzbq %r8b, %r8
    movaps %xmm4, %xmm9
    mulps %xmm9, %xmm9
    hlt
