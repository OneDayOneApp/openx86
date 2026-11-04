    movq $0x800000, %rsp
    movq $0x600000, %rbx
    movabsq $356221673847166784, %rax
    movabsq $5354108436562067341, %rcx
    movabsq $362859016545822371, %rdx
    movabsq $6972492833830841050, %rsi
    movabsq $2571913845811194390, %rdi
    movabsq $2849154168810668649, %r8
    movabsq $7867042199381895954, %r9
    movabsq $6734137821486832562, %r10
popq %rbx
andq $-17, %r8
movb $14, %cl
rolq %cl, %rax
movb $188, %r8b
notq %r10
movl $99812921, %r13d
movl $1569863894, %esi
cmpq %r11, %r13
setg %r8b
movzbq %r8b, %r8
movl %edx, 8(%rbx)
movdqa %xmm13, 0(%rbx)
orq $-53, %rbx
popq %r13
addq %rsi, %rdi
andq %r15, %r11
addq $-37, %r11
movb $238, %cl
movq 16(%rbx), %rdx
movq %r10, 96(%rbx)
sbbq %r10, %r14
shrq $6, %r14
addq %rbx, %rdx
movl $521270542, %eax
    movabsq $0xf0000000, %r15
    movq $0, (%r15)
