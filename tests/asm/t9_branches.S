# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    xorq %r13, %r13
    xorq %r14, %r14
    movq $5, %rax
    movq $3, %rbx
    cmpq %rbx, %rax
    jg 1f
    addq $1, %r14
1:
    jle 2f
    addq $1, %r14
2:
    je 3f
    addq $1, %r14
3:
    jne 4f
    addq $1, %r14
4:
    jl 5f
    addq $1, %r14
5:
    jae 6f
    addq $1, %r14
6:
    jb 7f
    addq $1, %r14
7:
    testq %rax, %rax
    jns 8f
    addq $1, %r14
8:
    js 9f
    addq $1, %r14
9:
    jz 10f
    addq $1, %r14
10:
    jnz 11f
    addq $1, %r14
11:
    movq %r13, -8(%rsp)
    movq %r14, -16(%rsp)
    movq $0xf0000000, %r15
    movq $0, (%r15)
