.text
.globl _start
_start:
    movq $0x800000, %rsp
    leaq func(%rip), %rax
    callq *%rax
    movq $11, %r8
    leaq after(%rip), %rbx
    jmp *%rbx
    movq $99, %r9
after:
    hlt
func:
    movq $22, %r10
    retq
