.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $-3, %r15
    negq %r15
    movq $0xf0000000, %r14
    movq $0, (%r14)
