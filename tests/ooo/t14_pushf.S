.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $0x1234, %rax
    cmpq $0x1234, %rax
    pushfq
    popq %rbx
    pushq %rbx
    popfq
    setz %cl
    movzbq %cl, %rcx
    movq $0x55, %rdx
    pushq %rdx
    popfq
    setc %sil
    movzbq %sil, %rsi
    hlt
