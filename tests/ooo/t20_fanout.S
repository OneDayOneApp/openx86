.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $0x600000, %rbx
    movq $42, (%rbx)
    movq (%rbx), %rax
    movq %rax, %r8
    movq %rax, %r9
    movq %rax, %r10
    movq %rax, %r11
    addq %r8, %r9
    addq %r10, %r11
    hlt
