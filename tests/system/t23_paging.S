# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp

    # page tables at 0x500000
    movq $0x500000, %rbx
    movq $0x501003, 0x0000(%rbx)          # PML4[0]
    movq $0x502003, 0x1000(%rbx)          # PDPT[0]
    movq $0x503003, 0x2010(%rbx)          # PD[2] -> PT 0x503000
    movq $0x504003, 0x2018(%rbx)          # PD[3] -> PT 0x504000
    movq $0x400003, 0x3000(%rbx)          # VA 0x400000 -> PA 0x400000 (code)
    movq $0x700003, 0x4000(%rbx)          # VA 0x600000 -> PA 0x700000
    movq $0x7ff003, 0x4ff8(%rbx)          # VA 0x7ff000 -> PA 0x7ff000 (stack)

    # sentinels
    movl $0x12345678, 0x700000
    movl $0x1111, 0x600000

    # enable paging
    movq $0x500000, %rax
    movq %rax, %cr3
    movq $0x20, %rax
    movq %rax, %cr4
    movq $0x80000001, %rax
    movq %rax, %cr0

    movl 0x600000, %r8d                   # translated load -> 0x12345678
    movl $0x0badf00d, 0x600008            # translated store -> PA 0x700008

    # disable paging again
    movq $1, %rax
    movq %rax, %cr0

    movl 0x700008, %r9d
    movq $1, %r12
    cmpl $0x12345678, %r8d
    jne 1f
    cmpl $0x0badf00d, %r9d
    jne 1f
    xorq %r12, %r12
1:
    movq $0xf0000000, %r15
    movq %r12, (%r15)
