.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $0x600000, %rbx
    movq $5, (%rbx)
    movq $10, %rax
    addq (%rbx), %rax
    movq $3, %rcx
    subq (%rbx), %rcx
    xorq (%rbx), %rcx
    andq (%rbx), %rax
    orq (%rbx), %rcx
    cmpq (%rbx), %rax
    seta %r8b
    movzbq %r8b, %r8
    hlt
