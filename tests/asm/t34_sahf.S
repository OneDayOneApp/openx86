# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $0xd700, %rax
    sahf
    setc %al
    setz %cl
    sets %dl
    setp %sil
    movzbq %al, %r8
    movzbq %cl, %r9
    movzbq %dl, %r10
    movzbq %sil, %r11
    movq $0xf0000000, %r15
    movq $0, (%r15)
