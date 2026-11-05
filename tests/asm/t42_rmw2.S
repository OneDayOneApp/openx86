# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $0x600000, %rbx
    movq $10, (%rbx)
    incq (%rbx)
    decq (%rbx)
    negq (%rbx)
    notq (%rbx)
    shlq $3, (%rbx)
    shrq $1, (%rbx)
    movq $12, (%rbx)
    movq $2, %rcx
    sarq %cl, (%rbx)
    rolq $1, (%rbx)
    rorq $1, (%rbx)
    movq (%rbx), %r8
    movq $0xf0000000, %r15
    movq $0, (%r15)
