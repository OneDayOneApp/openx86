# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $0x1111, %rax
    movq $0x2222, %rbx
    pushq %rax
    pushq %rbx
    movq $0x3333, %rcx
    pushq %rcx
    popq %rdx
    popq %rsi
    popq %rdi
    pushq $0x4444
    popq %r8
    movq $0x5555, %r9
    pushq %r9
    popq %r10
    hlt
