# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    xorq %r12, %r12
    xorq %r13, %r13
    xorq %r14, %r14
    movq $5, %rax
    movq $3, %rbx
    cmpq %rbx, %rax
    jg 1f
    addq $1, %r14
1:
    movq %r14, -8(%rsp)
    jle 2f
    addq $1, %r13
2:
    movq %r14, -16(%rsp)
    cmpq $5, %rax
    je 3f
    addq $1, %r14
3:
    movq %r14, -24(%rsp)
    jne 4f
    addq $1, %r13
4:
    movq %r14, -32(%rsp)
    cmpq %rax, %rbx
    jl 5f
    addq $1, %r14
5:
    movq %r14, -40(%rsp)
    jae 6f
    addq $1, %r14
6:
    movq %r14, -48(%rsp)
    jb 7f
    addq $1, %r13
7:
    movq %r14, -56(%rsp)
    testq %rax, %rax
    jns 8f
    addq $1, %r14
8:
    movq %r14, -64(%rsp)
    js 9f
    addq $1, %r14
9:
    movq %r14, -72(%rsp)
    jz 10f
    addq $1, %r14
10:
    movq %r14, -80(%rsp)
    jnz 11f
    addq $1, %r13
11:
    movq %r14, -88(%rsp)
    movq $0xf0000000, %r15
    movq $0, (%r15)
