# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $-1, %rax
    movzbq %al, %rbx
    movswq %ax, %rcx
    movslq %eax, %rdx
    movq $0x7f, %r8
    movsbq %r8b, %r9
    movq $0x80, %r10
    movsbq %r10b, %r11
    cmpl %ecx, %edx
    sete %al
    movzbq %al, %r12
    movq $5, %r13
    movq $9, %r14
    cmpq %r13, %r14
    cmovl %r13, %r14
    movq $-3, %r15
    negq %r15
    hlt
