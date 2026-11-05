# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $100, %rax
    movq $3, %rbx
    xorq %rdx, %rdx
    divq %rbx
    movq %rax, %r8
    movq %rdx, %r9
    movq $-50, %rax
    cqto
    movq $7, %rbx
    idivq %rbx
    movq %rax, %r10
    movq %rdx, %r11
    movq $0xf0000000, %r15
    movq $0, (%r15)
