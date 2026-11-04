.text
.globl _start
_start:
    movq $0x800000, %rsp

    # page tables
    movq $0x500000, %rbx
    movq $0x501003, 0x0000(%rbx)
    movq $0x502003, 0x1000(%rbx)
    movq $0x503003, 0x2010(%rbx)
    movq $0x504003, 0x2018(%rbx)
    movq $0x400003, 0x3000(%rbx)          # VA 0x400000 -> PA 0x400000 (code)
    movq $0x700003, 0x4000(%rbx)
    movq $0x7ff003, 0x4ff8(%rbx)

    # IDT gate for vector 14 at 0x5100e0
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
    movq $0x5100e0, %rbx
    movq %rcx, (%rbx)
    movq %rax, %rdx
    shrq $32, %rdx
    movq %rdx, 8(%rbx)

    movw $0x0fff, 0x520000
    movq $0x510000, 0x520002
    movq $0x520000, %rax
    lidt (%rax)

    movq $0x500000, %rax
    movq %rax, %cr3
    movq $0x80000001, %rax
    movq %rax, %cr0

    movq 0x610000, %r8          # unmapped -> #PF
    movq $0xf0000000, %r15
    movq $0x99, (%r15)

handler:
    movq $1, %rax
    movq %rax, %cr0             # disable paging
    movq $0xf0000000, %r15
    movq $0x50, (%r15)
