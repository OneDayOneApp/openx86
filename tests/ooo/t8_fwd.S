# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $0x600100, %rbx
    movq $0x1122334455667788, %rax
    movq %rax, (%rbx)
    movq (%rbx), %rcx
    movq %rcx, 8(%rbx)
    addq $1, %rcx
    movq %rcx, 16(%rbx)
    movq 16(%rbx), %rdx
    movl $0x0abbccdd, 24(%rbx)
    movl 24(%rbx), %esi
    hlt
