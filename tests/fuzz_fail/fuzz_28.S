    movq $0x800000, %rsp
    movq $0x600000, %rbx
    movabsq $2413796482811095290, %rax
    movabsq $3284358380458321149, %rcx
    movabsq $2444133020017739918, %rdx
    movabsq $8561107212335819449, %rsi
    movabsq $7702769483512745192, %rdi
    movabsq $3935228253601461639, %r8
    movabsq $7225169336580826061, %r9
    movabsq $2418409527709321101, %r10
testq %rbx, %rbx
sete %r8b
movzbq %r8b, %r8
addq $87, %rcx
decq %rdx
movq 64(%rbx), %r13
cmpq %rbx, %r12
setae %r8b
movzbq %r8b, %r8
addq %r14, %rsi
testq %r12, %rsi
setne %r8b
movzbq %r8b, %r8
sbbq %rdx, %rbx
incq %rax
rolq $7, %rdi
orq $-39, %r10
subq $-31, %rsi
movq %rax, %xmm13
negq %r12
psubd %xmm12, %xmm3
movq 112(%rbx), %r10
pushq %r12
movb $9, %cl
shrq %cl, %r9
movq %rax, 0(%rbx)
movb $14, %cl
rolq %cl, %r12
addq %r12, %r9
negq %rcx
orq $-76, %rbx
testq %rdx, %rcx
sets %r8b
movzbq %r8b, %r8
movl %r9d, 16(%rbx)
orq %rdx, %r14
movw %r9w, 80(%rbx)
    movabsq $0xf0000000, %r15
    movq $0, (%r15)
