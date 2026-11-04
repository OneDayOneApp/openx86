.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $0x600000, %rbx
    movq $0x12345678, %rax
    movq %rax, (%rbx)
    movq $0x0badf00d, %rcx
    movq %rcx, 8(%rbx)
    movq 8(%rbx), %rdx
    movl (%rbx), %esi
    movw $0x0bcd, 16(%rbx)
    movzwq 16(%rbx), %rdi
    movsbq 17(%rbx), %r8
    movq %rax, 24(%rbx)
    movq 24(%rbx), %r9
    movb $0x5a, 32(%rbx)
    movzbq 32(%rbx), %r10
    hlt
