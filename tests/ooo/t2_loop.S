# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $5, %r12
    xorq %r13, %r13
1:
    addq $3, %r13
    decq %r12
    jnz 1b
    movq $0, %r14
    movq $8, %r15
2:
    addq %r15, %r14
    subq $1, %r15
    jnz 2b
    hlt
