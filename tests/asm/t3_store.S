# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $0x1122334455667788, %rax
    movq %rax, -8(%rsp)
    movq -8(%rsp), %r8
    movq $0xf0000000, %r15
    movq $0, (%r15)
