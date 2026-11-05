# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $0x600000, %rbx
    movq $0x1234, (%rbx)
    movq $0x5678, %rax
    xchgq %rax, (%rbx)
    movq (%rbx), %r8
    xchgq %rax, (%rbx)
    movq (%rbx), %r9
    hlt
