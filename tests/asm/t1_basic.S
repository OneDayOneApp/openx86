# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    xorq %rbp, %rbp

    # immediate / ALU
    movq $0x1122334455667788, %rax
    movq $0x1000, %rbx
    addq %rax, %rbx
    subq $0x200, %rbx
    andq $0xff00, %rbx
    orq $0x55, %rbx
    xorq %rax, %rbx
    testq %rbx, %rbx

    # shifts
    movq %rbx, %rcx
    shlq $3, %rcx
    shrq $2, %rcx
    sarq $1, %rcx
    rolq $5, %rcx
    rorq $2, %rcx

    # lea
    leaq 16(%rsp,%rax,2), %rdx

    # setcc / cmov
    cmpq %rax, %rbx
    setl %cl
    movzbq %cl, %rcx
    cmovg %rax, %rbx

    # stack
    pushq %rax
    pushq %rbx
    popq %rcx
    popq %rdx

    # memory through the stack
    movq %rax, -8(%rsp)
    movq -8(%rsp), %r8
    movl $0x12345678, -16(%rsp)
    movl -16(%rsp), %r9d
    movw $0xabcd, -24(%rsp)
    movw -24(%rsp), %r10w
    movb $0x5a, -32(%rsp)
    movzbq -32(%rsp), %r11

    # loop / branches
    movq $5, %r12
1:
    addq $3, %r13
    decq %r12
    jnz 1b

    # call / ret
    call 2f
    jmp 3f
2:
    addq $7, %r13
    ret
3:

    # multiply / divide
    movq $123, %rax
    movq $7, %rbx
    imulq %rbx, %rax
    imulq $9, %rbx, %rcx
    xorq %rdx, %rdx
    movq $1000, %rax
    movq $7, %rbx
    divq %rbx
    xorq %rdx, %rdx
    movq $-1000, %rax
    idivq %rbx

    # store results where the difftest can see them
    movq %rax, -40(%rsp)
    movq %rdx, -48(%rsp)
    movq %r13, -56(%rsp)
    movq %r8, -64(%rsp)

    # halt with success (MMIO through a register keeps the address canonical)
    movq $0xf0000000, %r15
    movq $0, (%r15)
