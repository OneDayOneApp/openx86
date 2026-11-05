# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    xorq %r10, %r10
    movq $3, %r8
outer:
    movq $4, %r9
inner:
    addq %r8, %r10
    decq %r9
    jnz inner
    decq %r8
    jnz outer

    movq $0, %r11
    movq $6, %rcx
fwd:
    cmpq $3, %rcx
    jl skip
    addq %rcx, %r11
skip:
    decq %rcx
    jnz fwd
    hlt
