# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    lea dq(%rip), %r8
    movdqa (%r8), %xmm0
    cvtdq2ps %xmm0, %xmm1
    movdqa %xmm1, -16(%rsp)
    cvtps2dq %xmm1, %xmm2
    movdqa %xmm2, -32(%rsp)
    cvtdq2pd %xmm0, %xmm3
    movdqa %xmm3, -48(%rsp)
    cvtpd2dq %xmm3, %xmm4
    movdqa %xmm4, -64(%rsp)
    cvttpd2dq %xmm3, %xmm5
    movdqa %xmm5, -80(%rsp)
    movq $0xf0000000, %r15
    movq $0, (%r15)

.p2align 4
dq:
    .long 1
    .long -2
    .long 3
    .long -4
