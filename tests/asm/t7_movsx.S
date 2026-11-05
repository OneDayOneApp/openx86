# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $0xdeadbeef, %rax
    movb $0xf0, %al
    movsbl %al, %edx
    movq %rdx, -8(%rsp)
    movq $0xf0000000, %r15
    movq $0, (%r15)
