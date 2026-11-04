    movq $0x800000, %rsp
    movq $0x600000, %rbx
    movabsq $1903077362797705724, %rax
    movabsq $7305985015658163149, %rcx
    movabsq $2858733919356307011, %rdx
    movabsq $1226987017717469011, %rsi
    movabsq $7408046685572742419, %rdi
    movabsq $4093852239982008774, %r8
    movabsq $5103893919250842291, %r9
    movabsq $3185098314989004691, %r10
xorq %rbx, %rax
cmpq %r14, %rsi
setg %r8b
movzbq %r8b, %r8
xorq %rsi, %r12
incq %r15
cmpq %rdi, %r12
seta %r8b
movzbq %r8b, %r8
orq %r9, %rsi
movabsq $5538556345519214582, %r15
movabsq $7635956733591140643, %rsi
rorq $2, %rsi
andq $-106, %rsi
movl $1472500625, %eax
movdqa %xmm6, %xmm2
imulq %rsi, %rdx
notq %r13
pushq %r10
adcq %rcx, %r15
movb $14, %r13b
popq %rbx
adcq %rdx, %rsi
movq %rdi, 16(%rbx)
andq $37, %rax
andq $82, %rdx
testq %r12, %r15
setne %r8b
movzbq %r8b, %r8
cmpq %rbx, %r9
setg %r8b
movzbq %r8b, %r8
orq $-47, %r11
incq %rdi
movb $58, %r9b
negq %r11
imulq %rbx, %r8
subq %rax, %rax
movb $176, %r13b
por %xmm4, %xmm1
andq $-66, %rax
movq %r8, 48(%rbx)
movq %r9, 104(%rbx)
    movabsq $0xf0000000, %r15
    movq $0, (%r15)
