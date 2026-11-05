# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp

    # build IDT gate for vector 6 (UD)
    leaq handler(%rip), %rax
    movq %rax, %rcx
    andq $0xffff, %rcx
    movq %rax, %rdx
    shrq $16, %rdx
    andq $0xffff, %rdx
    shlq $48, %rdx
    orq %rdx, %rcx
    movq $0x8, %rdx
    shlq $16, %rdx
    orq %rdx, %rcx
    movq $0x8e, %rdx
    shlq $40, %rdx
    orq %rdx, %rcx
    movq $0x510060, %rbx
    movq %rcx, (%rbx)
    movq %rax, %rdx
    shrq $32, %rdx
    movq %rdx, 8(%rbx)

    # IDTR descriptor at 0x520000
    movw $0x0fff, 0x520000
    movq $0x510000, 0x520002
    movq $0x520000, %rax
    lidt (%rax)

    ud2

    # should not return here
    movq $0xf0000000, %r15
    movq $0x99, (%r15)

handler:
    movq $0xf0000000, %r15
    movq $0x42, (%r15)
