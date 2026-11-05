# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $-50, %rax
    cqto
    movq %rdx, %r8
    movq $0xf0000000, %r15
    movq $0, (%r15)
