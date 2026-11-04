.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $3, %rcx
    movq $0, %rax
L1:
    incq %rax
    loopne L1
    movq %rax, %r8
    movq $0, %rcx
    jrcxz L2
    movq $0xdead, %r9
L2:
    movq $0x11, %r10
    movq $0xf0000000, %r15
    movq $0, (%r15)
