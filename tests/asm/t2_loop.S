.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $2, %r12
1:
    decq %r12
    jne 1b
    movq $0xf0000000, %r15
    movq $0, (%r15)
