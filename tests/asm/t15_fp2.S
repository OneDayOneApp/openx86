# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $0x3ff8000000000000, %rax
    movq %rax, %xmm0
    movq %xmm0, %r8
    movq $0x4002000000000000, %rax
    movq %rax, %xmm1
    addsd %xmm1, %xmm0
    movq %xmm0, %r9
    movq $0xf0000000, %r15
    movq $0, (%r15)
