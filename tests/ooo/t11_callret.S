.text
.globl _start
_start:
    movq $0x800000, %rsp
    callq func1
    movq $0x11, %rax
    callq func2
    movq $0x33, %rcx
    hlt

func1:
    movq $0x22, %rbx
    retq

func2:
    pushq %rax
    movq $0x44, %rdx
    popq %rsi
    retq
