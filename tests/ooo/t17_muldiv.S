.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $7, %rax
    movq $3, %rbx
    mulq %rbx
    movq %rax, %r8
    movq %rdx, %r9
    movq $11, %rax
    imulq %rbx
    movq %rax, %r12
    movq %rdx, %r13
    hlt
