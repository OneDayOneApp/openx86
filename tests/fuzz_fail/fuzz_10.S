    movq $0x800000, %rsp
    movq $0x600000, %rbx
    movabsq $7911606630143721137, %rax
    movabsq $3801841278808493863, %rcx
    movabsq $2955713685324347011, %rdx
    movabsq $6046341174845128633, %rsi
    movabsq $4611624652261463927, %rdi
    movabsq $821920247944897630, %r8
    movabsq $7039387941527265832, %r9
    movabsq $5231832632959431939, %r10
movq %rsi, %xmm8
psubd %xmm11, %xmm4
movl %eax, 0(%rbx)
subq $27, %rbx
movq 80(%rbx), %rbx
por %xmm14, %xmm13
decq %r15
orq $-110, %rbx
movl $2825620078, %r9d
movw %cx, 32(%rbx)
movb $4, %cl
rorq %cl, %rdi
subq %r9, %rdx
adcq %r8, %r8
movq %rsi, 32(%rbx)
testq %r10, %rdx
setne %r8b
movzbq %r8b, %r8
movq %r12, 40(%rbx)
imulq %r14, %rdi
notq %rbx
movabsq $6117995899033364073, %r10
decq %rcx
orq $-47, %r8
movb $8, %cl
rolq %cl, %r12
movq 16(%rbx), %r9
imulq %r12, %r9
movb $11, %cl
shrq %cl, %r8
movq 40(%rbx), %r13
movq 8(%rbx), %r8
movabsq $932906079956759669, %r9
incq %rdx
imulq %r15, %r14
movq %r15, 104(%rbx)
movw %di, 64(%rbx)
movdqa %xmm14, %xmm13
movw %dx, 56(%rbx)
incq %r12
imulq %rax, %r14
xorq %r8, %r15
andq $-105, %rcx
testq %r10, %r14
setne %r8b
movzbq %r8b, %r8
imulq %rdx, %rdi
testq %r8, %r14
setle %r8b
movzbq %r8b, %r8
subq %r11, %rbx
movb $9, %cl
shrq %cl, %rcx
movl 104(%rbx), %r14d
movl %r10d, 48(%rbx)
xorq $-69, %rdi
cmpq %r14, %r12
sete %r8b
movzbq %r8b, %r8
movb $6, %cl
rorq %cl, %r8
movl $3423601478, %r14d
movl $1547142298, %edi
movb $14, %cl
shrq %cl, %r12
pxor %xmm9, %xmm15
rolq $6, %r12
orq $116, %r9
pushq %r13
movl %r14d, 96(%rbx)
    movabsq $0xf0000000, %r15
    movq $0, (%r15)
