# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp

    # unaligned stores/loads of all widths
    movq $0x1122334455667788, %rax
    movq %rax, -1(%rsp)
    movq -1(%rsp), %rbx
    movl $0xdeadbeef, -9(%rsp)
    movl -9(%rsp), %ecx
    movw $0x1234, -17(%rsp)
    movw -17(%rsp), %dx
    movb $0x7f, -25(%rsp)
    movb -25(%rsp), %r8b

    # SIB addressing: base + index*scale + disp
    leaq -512(%rsp), %r9
    movq $0x1111, (%r9)
    movq $0x2222, 8(%r9)
    movq $0x3333, 16(%r9)
    movq $1, %rcx
    movq 0(%r9,%rcx,8), %r10
    movq 8(%r9,%rcx,8), %r11
    movq -8(%r9,%rcx,8), %r12

    # RIP-relative addressing into inline data
    leaq blk(%rip), %r13
    movq (%r13), %r14
    movl 8(%r13), %r15d
    movq 16(%r13), %rax
    jmp 1f
blk:
    .quad 0x8877665544332211
    .long 0xcafebabe
    .quad 0x0123456789abcdef
1:

    # spill results
    movq %rbx, -32(%rsp)
    movq %r10, -40(%rsp)
    movq %r11, -48(%rsp)
    movq %r12, -56(%rsp)
    movq %r14, -64(%rsp)
    movq %rax, -72(%rsp)

    movq $0xf0000000, %r15
    movq $0, (%r15)
