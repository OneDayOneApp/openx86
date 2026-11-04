    movq $0x800000, %rsp
    movq $0x600000, %rbx
    movabsq $2226308815651685679, %rax
    movabsq $3680076041817235136, %rcx
    movabsq $6404134666269274130, %rdx
    movabsq $1986120937536621482, %rsi
    movabsq $7597596285007037196, %rdi
    movabsq $421704023878530549, %r8
    movabsq $2100928827606332199, %r9
    movabsq $8404465309349414043, %r10
shlq $1, %r14
sbbq %r14, %r15
movb $5, %cl
rolq %cl, %r12
cmpq %r8, %rbx
setb %r8b
movzbq %r8b, %r8
sbbq %rbx, %rdx
imulq %rcx, %r10
movdqa 112(%rbx), %xmm14
testq %rcx, %r10
setae %r8b
movzbq %r8b, %r8
notq %r10
addq %rbx, %rcx
movl $1940472757, %r9d
pand %xmm8, %xmm4
sarq $4, %r13
andq $-105, %r12
movl $3230017054, %ebx
imulq %rcx, %r9
movq 40(%rbx), %rax
sarq $2, %rcx
movb $6, %cl
sarq %cl, %r9
testq %r9, %r12
setne %r8b
movzbq %r8b, %r8
testq %r12, %rdx
sete %r8b
movzbq %r8b, %r8
cmpq %r12, %rsi
sets %r8b
movzbq %r8b, %r8
    movabsq $0xf0000000, %r15
    movq $0, (%r15)
