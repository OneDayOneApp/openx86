.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $1, %rax
    movq $2, %rbx
    movq $3, %rcx
    movq $4, %rdx
    movq $5, %rsi
    movq $6, %rdi
    movq $7, %r8
    movq $8, %r9
    addq $10, %rax
    addq $20, %rbx
    addq $30, %rcx
    addq $40, %rdx
    addq $50, %rsi
    addq $60, %rdi
    addq $70, %r8
    addq $80, %r9
    xorq %rax, %rcx
    xorq %rbx, %rdx
    xorq %rsi, %rdi
    xorq %r8, %r9
    hlt
