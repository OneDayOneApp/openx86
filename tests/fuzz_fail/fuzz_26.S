    movq $0x800000, %rsp
    movq $0x600000, %rbx
    movabsq $3784509138622753076, %rax
    movabsq $1046987216624322245, %rcx
    movabsq $769898470683004723, %rdx
    movabsq $7547313180820355928, %rsi
    movabsq $531111562100767133, %rdi
    movabsq $4100373648725916650, %r8
    movabsq $2387218359325983082, %r9
    movabsq $6719756737496255191, %r10
xorq $-113, %rax
xorq $-65, %r10
movq %rax, 112(%rbx)
testq %r12, %r10
setge %r8b
movzbq %r8b, %r8
movdqa %xmm13, 48(%rbx)
movb $191, %sil
movb $1, %cl
rolq %cl, %r11
subq %rdx, %r10
movl %eax, 8(%rbx)
movb $2, %cl
shrq %cl, %rdi
movq %r8, 8(%rbx)
rolq $3, %r11
orq %r14, %rcx
xorq %rdi, %r14
testq %r14, %r12
setns %r8b
movzbq %r8b, %r8
notq %r11
shlq $1, %rax
movq %r11, 120(%rbx)
movb $184, %r11b
movb $91, %r13b
movl $703856649, %esi
subq $-94, %rcx
testq %r15, %rax
sete %r8b
movzbq %r8b, %r8
movq %rsi, 120(%rbx)
notq %rax
subq %rbx, %r9
andq $-54, %rbx
testq %r10, %r8
seta %r8b
movzbq %r8b, %r8
movdqa %xmm2, 0(%rbx)
movl $1040891251, %edx
movl $1390916709, %ebx
movq %r9, 56(%rbx)
    movabsq $0xf0000000, %r15
    movq $0, (%r15)
