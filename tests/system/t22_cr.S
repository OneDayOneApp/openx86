# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $0x500000, %rax
    movq %rax, %cr3
    movq %cr3, %rbx
    movq $1, %r12
    cmpq $0x500000, %rbx
    jne 1f
    xorq %r12, %r12
1:
    movq $0xf0000000, %r15
    movq %r12, (%r15)
