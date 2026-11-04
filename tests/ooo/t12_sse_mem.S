.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $0x600000, %rbx
    movq $0x1122334455667788, %rax
    movq %rax, %xmm0
    movdqa %xmm0, (%rbx)
    movdqa (%rbx), %xmm1
    movdqa %xmm1, 16(%rbx)
    movq %xmm1, 32(%rbx)
    movq 32(%rbx), %rcx
    movss %xmm1, 40(%rbx)
    movss 40(%rbx), %xmm2
    movsd %xmm1, 48(%rbx)
    movsd 48(%rbx), %xmm3
    movd %xmm1, 56(%rbx)
    movd 56(%rbx), %xmm4
    hlt
