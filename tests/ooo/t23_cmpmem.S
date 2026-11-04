.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $0x600000, %rbx
    movq $0x4000000000000000, %rax
    movq %rax, %xmm0
    movq %rax, (%rbx)
    movq $0x3ff0000000000000, %rcx
    movq %rcx, 8(%rbx)
    movq %rcx, %xmm1
    ucomisd (%rbx), %xmm1
    seta %r8b
    ucomisd 8(%rbx), %xmm0
    seta %r9b
    movzbq %r8b, %r8
    movzbq %r9b, %r9
    hlt
