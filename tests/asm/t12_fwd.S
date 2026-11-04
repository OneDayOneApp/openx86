.text
.globl _start
_start:
    movq $0x800000, %rsp
    xorq %r14, %r14
    movq %r14, -8(%rsp)
    addq $1, %r14
    movq %r14, -16(%rsp)
    addq $1, %r14
    movq %r14, -24(%rsp)
    movq $0xf0000000, %r15
    movq $0, (%r15)
