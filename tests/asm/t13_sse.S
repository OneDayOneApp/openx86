.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $0x1122334455667788, %rax
    movq %rax, %xmm0
    movdqa %xmm0, %xmm1
    paddd %xmm1, %xmm0
    psubd %xmm1, %xmm0
    pxor %xmm2, %xmm2
    movdqa %xmm0, %xmm3
    movd %xmm0, %ecx
    movq %xmm1, %rdx

    lea blk(%rip), %r8
    movdqa (%r8), %xmm4
    movdqa %xmm4, %xmm5
    paddd %xmm5, %xmm4
    movdqa %xmm4, 16(%r8)

    comisd %xmm1, %xmm0
    sete %al
    movzbq %al, %r9

    movq %rcx, -8(%rsp)
    movq %rdx, -16(%rsp)
    movq %r9, -24(%rsp)
    movq $0xf0000000, %r15
    movq $0, (%r15)

.p2align 4
blk:
    .quad 0x1111222233334444
    .quad 0x5555666677778888
    .quad 0x9999aaaabbbbcccc
    .quad 0xddddeeeeffff0000
