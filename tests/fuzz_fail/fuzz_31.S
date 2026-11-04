    movq $0x800000, %rsp
    movq $0x600000, %rbx
    movabsq $2073392943015020223, %rax
    movabsq $7246570297989790990, %rcx
    movabsq $2558396058591010851, %rdx
    movabsq $2074217651814841083, %rsi
    movabsq $4275998764214495688, %rdi
    movabsq $2719072482060158736, %r8
    movabsq $2519429083109301655, %r9
    movabsq $8259546830060968199, %r10
movq 16(%rbx), %r11
addq %r14, %r8
decq %rbx
notq %rdi
cmpq %rbx, %rbx
setns %r8b
movzbq %r8b, %r8
movb $1, %cl
rorq %cl, %r12
adcq %rdx, %rbx
sbbq %rax, %rcx
movl 48(%rbx), %r12d
sbbq %r13, %rdi
adcq %r12, %r15
orq $6, %rax
negq %rdi
pushq %r12
cmpq %r15, %rdi
setle %r8b
movzbq %r8b, %r8
subq $23, %r10
movb $-114, %dl
testq %rdi, %r13
setne %r8b
movzbq %r8b, %r8
andq $-28, %r12
imulq %rbx, %r14
    movabsq $0xf0000000, %r15
    movq $0, (%r15)
