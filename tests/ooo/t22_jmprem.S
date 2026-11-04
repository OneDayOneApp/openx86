.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $0x600000, %rbx
    leaq after(%rip), %rax
    movq %rax, (%rbx)
    jmp *(%rbx)
    movq $99, %r9
after:
    movq $0x11, %r8
    hlt
