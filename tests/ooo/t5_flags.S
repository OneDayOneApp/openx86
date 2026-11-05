# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $-1, %rax
    addq $1, %rax
    movq $0, %rbx
    adcq $0, %rbx
    movq $0, %rcx
    sbbq $0, %rcx
    movq $0x1234, %rdx
    movq $4, %rsi
    movq %rsi, %rcx
    shlq %cl, %rdx
    rorq $3, %rdx
    rolq $1, %rdx
    negq %rdx
    notq %rdx
    testq %rdx, %rdx
    setne %al
    movzbq %al, %r15
    orq $0x100, %rdx
    andq $0x1ff, %rdx
    xorq %rdx, %rdx
    cmpq $0, %rdx
    setz %al
    movzbq %al, %r14
    hlt
