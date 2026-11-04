    movq $0x800000, %rsp
    movq $0x600000, %rbx
    movabsq $6171537212193520327, %rax
    movabsq $3651341098826047620, %rcx
    movabsq $9029964174636126057, %rdx
    movabsq $9113331313822198196, %rsi
    movabsq $8850241299192177842, %rdi
    movabsq $8454716653285152512, %r8
    movabsq $4879722870911573114, %r9
    movabsq $4681800793441225020, %r10
psubd %xmm3, %xmm10
subq $-26, %rbx
decq %r14
movdqa %xmm15, 64(%rbx)
movl $3122889036, %edi
movq %r11, 64(%rbx)
xorq $27, %r13
movq %r9, 32(%rbx)
movl %r12d, 40(%rbx)
orq %r12, %r12
movb $3, %cl
shrq %cl, %r8
pxor %xmm6, %xmm8
pand %xmm15, %xmm2
imulq %rcx, %rbx
cmpq %rcx, %r10
setge %r8b
movzbq %r8b, %r8
cmpq %r8, %r14
setbe %r8b
movzbq %r8b, %r8
subq %r9, %r8
shrq $3, %r8
movq %rdi, %xmm12
cmpq %rsi, %rdx
seta %r8b
movzbq %r8b, %r8
pushq %r14
sbbq %rax, %r10
movq 24(%rbx), %r9
movabsq $4747444861311203970, %r14
movb $15, %cl
rolq %cl, %r9
shrq $7, %rbx
adcq %r13, %r12
orq $-81, %rax
decq %r15
movq %rbx, 64(%rbx)
movabsq $4254688404629143920, %rdx
    movabsq $0xf0000000, %r15
    movq $0, (%r15)
