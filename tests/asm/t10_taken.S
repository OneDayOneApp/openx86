# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    xorq %r14, %r14
    movq $5, %rax
    movq $3, %rbx
    cmpq %rbx, %rax
    jg 1f
    addq $1, %r14
1:
    addq $1, %r14
    movq %r14, -8(%rsp)
    movq $0xf0000000, %r15
    movq $0, (%r15)
