# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $0x600000, %rbx
    movq $10, (%rbx)
    movq $5, %rax
    addq %rax, (%rbx)
    movq $3, %rcx
    subq %rcx, (%rbx)
    movq $0xf0, %rdx
    andq %rdx, (%rbx)
    movq (%rbx), %r8
    cmpq $0, %r8
    sete %r9b
    movzbq %r9b, %r9
    movq $0xf0000000, %r15
    movq $0, (%r15)
