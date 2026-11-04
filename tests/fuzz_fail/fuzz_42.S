    movq $0x800000, %rsp
    movq $0x600000, %rbx
    movabsq $4517457392071889495, %rax
    movabsq $2574020394472462046, %rcx
    movabsq $1890702223848595625, %rdx
    movabsq $586287033698423193, %rsi
    movabsq $1728372192399379054, %rdi
    movabsq $4291835990902352011, %r8
    movabsq $7738774760351418614, %r9
    movabsq $8286444275301796832, %r10
pushq %r15
movb $229, %r14b
sarq $7, %rsi
incq %rcx
movb $8, %cl
shlq %cl, %rdi
movl $2303082117, %r13d
shlq $6, %r10
cmpq %r15, %rdi
setbe %r8b
movzbq %r8b, %r8
movdqa %xmm2, 64(%rbx)
movb $-77, %r15b
movb $11, %cl
sarq %cl, %r9
adcq %rdi, %rbx
psubd %xmm8, %xmm2
movl %edx, 112(%rbx)
movb $7, %cl
sarq %cl, %r12
movq %rax, %xmm10
notq %rsi
movabsq $3922267417137298135, %rbx
testq %r9, %r8
sets %r8b
movzbq %r8b, %r8
xorq %rdx, %rbx
movq 104(%rbx), %r13
pushq %rbx
imulq %rcx, %r14
movb $-50, %r15b
testq %rdx, %r14
sets %r8b
movzbq %r8b, %r8
testq %rcx, %r8
seta %r8b
movzbq %r8b, %r8
movq %r10, 0(%rbx)
    movabsq $0xf0000000, %r15
    movq $0, (%r15)
