# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp

    # page tables at 0x500000 (physical, paging off while building them)
    movq $0x500000, %rbx
    movq $0x501003, 0x0000(%rbx)          # PML4[0] -> PDPT
    movq $0x502003, 0x1000(%rbx)          # PDPT[0] -> PD
    movq $0x503003, 0x2010(%rbx)          # PD[2] -> PT (VA 0x400000-0x5fffff)
    movq $0x504003, 0x2018(%rbx)          # PD[3] -> PT (VA 0x600000-0x7fffff)
    movq $0x400003, 0x3000(%rbx)          # VA 0x400000 -> PA 0x400000 (code)
    movq $0x600003, 0x4000(%rbx)          # VA 0x600000 -> PA 0x600000 (data)
    movq $0x7ff003, 0x4ff8(%rbx)          # VA 0x7ff000 -> PA 0x7ff000 (stack)
    # PML4/PDPT/PD[4] stay zero: VA 0x800000 (page 8) is not mapped.

    # IDT gate for vector 14 at 0x5200e0, IDT base 0x520000
    leaq handler(%rip), %rax
    movq %rax, %rcx
    andq $0xffff, %rcx
    movq %rax, %rdx
    shrq $16, %rdx
    andq $0xffff, %rdx
    shlq $48, %rdx
    orq %rdx, %rcx
    movq $0x8, %rdx
    shlq $16, %rdx
    orq %rdx, %rcx
    movq $0x8e, %rdx
    shlq $40, %rdx
    orq %rdx, %rcx
    movq $0x5200e0, %rbx
    movq %rcx, (%rbx)
    movq %rax, %rdx
    shrq $32, %rdx
    movq %rdx, 8(%rbx)

    movw $0x0fff, 0x530000
    movq $0x520000, 0x530002
    movq $0x530000, %rax
    lidt (%rax)

    movq $0x500000, %rax
    movq %rax, %cr3
    movq $0x80000001, %rax
    movq %rax, %cr0                       # enable paging (fetch is translated now)

    # sanity check: data translation still works through the shared MMU
    movl $0x5a5a, 0x600000
    movl 0x600000, %r8d
    cmpl $0x5a5a, %r8d
    jne setup_fail

    movq $0x800000, %rax
    jmp *%rax                             # fetch from unmapped page -> #PF

setup_fail:
    movq $0x6e, %r12
    jmp disable_paging

handler:
    # trap frame: 0(%rsp)=error code, 8(%rsp)=RIP, 16(%rsp)=CS, 24(%rsp)=RFLAGS
    movq 0x8(%rsp), %r8                   # RIP must be the fetch address
    movq 0x0(%rsp), %r9                   # error code must have I/D bit set (0x10)
    movq $0x66, %r12
    cmpq $0x800000, %r8
    jne handler_bad
    cmpq $0x10, %r9
    jne handler_bad
    jmp disable_paging

handler_bad:
    movq $0x6f, %r12

disable_paging:
    # Code and stack are identity mapped, so physical fetch keeps working here.
    movq $1, %rax
    movq %rax, %cr0
    movq $0xf0000000, %r15
    movq %r12, (%r15)
