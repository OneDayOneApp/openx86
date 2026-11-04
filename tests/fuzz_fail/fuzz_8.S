    movq $0x800000, %rsp
    movq $0x600000, %rbx
    movabsq $2330393804099040876, %rax
    movabsq $1571390824258015408, %rcx
    movabsq $4564480497321256146, %rdx
    movabsq $3862704506556962227, %rsi
    movabsq $8469522993553429617, %rdi
    movabsq $8358818023028974057, %r8
    movabsq $9130819006586314152, %r9
    movabsq $3546033029056581805, %r10
shlq $1, %r9
pand %xmm8, %xmm13
xorq %rcx, %r15
movabsq $6958206584859933091, %r8
addq %rdi, %rbx
movdqa 48(%rbx), %xmm2
popq %r11
movb $122, %r10b
movdqa 112(%rbx), %xmm6
popq %rsi
movq 32(%rbx), %r8
subq %r10, %r13
negq %r12
movq 72(%rbx), %rbx
adcq %r10, %rsi
movl %r14d, 56(%rbx)
decq %rax
andq $8, %rbx
movl $1618885073, %r8d
movdqa %xmm13, 16(%rbx)
addq %rbx, %r14
xorq %rcx, %rax
sarq $4, %r15
subq $100, %r8
movb $9, %cl
rolq %cl, %rcx
movq %rcx, %xmm3
movq 120(%rbx), %rdx
movq %rdx, %xmm4
notq %r12
pushq %rsi
negq %rdi
movl $902295091, %r12d
cmpq %rax, %r15
setbe %r8b
movzbq %r8b, %r8
adcq %r8, %r9
    movabsq $0xf0000000, %r15
    movq $0, (%r15)
