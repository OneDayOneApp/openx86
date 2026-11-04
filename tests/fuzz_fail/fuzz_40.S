    movq $0x800000, %rsp
    movq $0x600000, %rbx
    movabsq $587463885240603165, %rax
    movabsq $5209842773310264809, %rcx
    movabsq $2368052226948221992, %rdx
    movabsq $6426647000926848573, %rsi
    movabsq $8130627863390247835, %rdi
    movabsq $528167709752541193, %r8
    movabsq $2395753827687222335, %r9
    movabsq $8497789262862079713, %r10
movabsq $2095619685863044561, %rdx
andq $-79, %rdx
movb $10, %cl
shlq %cl, %r15
subq $-45, %rsi
movq %r15, 72(%rbx)
movl 48(%rbx), %edx
movq 40(%rbx), %rcx
pushq %r14
pushq %r9
cmpq %rsi, %rcx
setle %r8b
movzbq %r8b, %r8
movq 80(%rbx), %r9
cmpq %r9, %rsi
setb %r8b
movzbq %r8b, %r8
movb $182, %al
movb $14, %cl
shrq %cl, %r14
sbbq %rbx, %r11
movl $955996783, %r8d
andq %rdx, %rsi
addq $44, %rdx
orq %rax, %rbx
addq %r14, %r10
subq $92, %r8
movq %r10, 56(%rbx)
movabsq $7366916613042169384, %r14
movb $201, %r15b
xorq %r14, %r14
movl $1830903869, %esi
movl %r10d, 64(%rbx)
pushq %r12
paddd %xmm0, %xmm6
pushq %r13
addq %r11, %r10
movw %ax, 72(%rbx)
movb $-44, %sil
addq %rax, %r15
decq %rdi
cmpq %r8, %r9
sete %r8b
movzbq %r8b, %r8
addq $-80, %r9
orq $7, %r9
movq %r11, 96(%rbx)
notq %r13
movq %r8, 88(%rbx)
testq %r13, %rdx
seta %r8b
movzbq %r8b, %r8
movb $174, %r10b
movabsq $2216214915825887978, %rax
adcq %r8, %r11
incq %rbx
cmpq %rcx, %r14
setl %r8b
movzbq %r8b, %r8
cmpq %r8, %r13
setbe %r8b
movzbq %r8b, %r8
movl $2095563386, %ebx
    movabsq $0xf0000000, %r15
    movq $0, (%r15)
