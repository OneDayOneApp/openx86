# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $5, %rax
    movq $3, %rbx
    cmpq %rbx, %rax
    jg 1f
    xorq %r12, %r12
1:
    jl 2f
    movq $1, %r12
2:
    cmpq %rax, %rbx
    jae 3f
    movq $2, %r13
3:
    testq %rax, %rax
    jz 4f
    movq $3, %r14
4:
    movq $0, %rcx
    cmpq $0, %rcx
    sete %cl
    movzbq %cl, %r15
    movq $7, %rdx
    cmovg %rax, %rdx
    hlt
