# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $0x400921fb54442d18, %rax
    movq %rax, %xmm0
    cvtsd2ss %xmm0, %xmm1
    movd %xmm1, %ecx
    cvtss2sd %xmm1, %xmm2
    movq %xmm2, %rdx
    movq $0xf0000000, %r15
    movq $0, (%r15)
