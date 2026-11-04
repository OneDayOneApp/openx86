.text
.globl _start
_start:
    movq $0x800000, %rsp

    # 1.5 + 2.25 = 3.75 (double)
    movq $0x3ff8000000000000, %rax
    movq %rax, %xmm0
    movq $0x4002000000000000, %rax
    movq %rax, %xmm1
    addsd %xmm1, %xmm0
    movq %xmm0, %r8

    # 3.0 * 2.0 = 6.0
    movq $0x4008000000000000, %rax
    movq %rax, %xmm2
    movq $0x4000000000000000, %rax
    movq %rax, %xmm3
    mulsd %xmm3, %xmm2
    movq %xmm2, %r9

    # 7.5 / 2.5 = 3.0
    movq $0x401e000000000000, %rax
    movq %rax, %xmm4
    movq $0x4004000000000000, %rax
    movq %rax, %xmm5
    divsd %xmm5, %xmm4
    movq %xmm4, %r10

    # int -> double -> int
    movl $1234, %eax
    cvtsi2sd %eax, %xmm6
    cvttsd2si %xmm6, %ecx

    # scalar float: 1.5f + 2.5f = 4.0f
    movl $0x3fc00000, %eax
    movd %eax, %xmm7
    movl $0x40200000, %eax
    movd %eax, %xmm8
    addss %xmm8, %xmm7
    movd %xmm7, %edx

    movq %r8, -8(%rsp)
    movq %r9, -16(%rsp)
    movq %r10, -24(%rsp)
    movq %rcx, -32(%rsp)
    movq %rdx, -40(%rsp)

    movq $0xf0000000, %r15
    movq $0, (%r15)
