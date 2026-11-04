.text
.globl _start
_start:
    movq $0x800000, %rsp

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
    movq $0x510200, %rbx
    movq %rcx, (%rbx)
    movq %rax, %rdx
    shrq $32, %rdx
    movq %rdx, 8(%rbx)

    movw $0x0fff, 0x520000
    movq $0x510000, 0x520002
    movq $0x520000, %rax
    lidt (%rax)

    int $0x20

    movq $0xf0000000, %r15
    movq $0x44, (%r15)

handler:
    movq $0x1234, %r12
    movq $0xf0000000, %r15
    movq $0x33, (%r15)
