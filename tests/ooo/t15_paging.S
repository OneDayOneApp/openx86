# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    movl $0x12345678, 0x700000

    movq $0x500000, %rbx
    movq $0x501003, 0x0000(%rbx)
    movq $0x502003, 0x1000(%rbx)
    movq $0x503003, 0x2010(%rbx)
    movq $0x504003, 0x2018(%rbx)
    movq $0x400003, 0x3000(%rbx)
    movq $0x700003, 0x4000(%rbx)

    movq $0x500000, %rax
    movq %rax, %cr3
    movq $0x80000001, %rax
    movq %rax, %cr0

    movl 0x600000, %r8d
    movl $0x0badf00d, 0x600008

    movq $1, %rax
    movq %rax, %cr0

    movl 0x700008, %r9d
    hlt
