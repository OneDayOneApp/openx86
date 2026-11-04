.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $0x600000, %rbx
    movq $10, (%rbx)
    movq $5, %rax
    xaddq %rax, (%rbx)
    movq (%rbx), %r8
    hlt
