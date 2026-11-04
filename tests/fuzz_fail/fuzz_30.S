    movq $0x800000, %rsp
    movq $0x600000, %rbx
    movabsq $5334732702343898178, %rax
    movabsq $554083094847207184, %rcx
    movabsq $3873976585178171865, %rdx
    movabsq $4745922644184399525, %rsi
    movabsq $7327014688773434453, %rdi
    movabsq $8513781360447535279, %r8
    movabsq $4476573511010339198, %r9
    movabsq $1340065736749361021, %r10
sbbq %r15, %r12
incq %r8
movabsq $5161401904890524938, %rbx
movb $3, %cl
rorq %cl, %rcx
testq %r9, %rsi
setl %r8b
movzbq %r8b, %r8
andq $-87, %r10
orq %r13, %rdi
addq %r8, %r13
movq %r10, 40(%rbx)
movl %r10d, 112(%rbx)
movq %r12, %xmm10
imulq %r9, %rsi
movq %rsi, 40(%rbx)
pand %xmm5, %xmm5
pand %xmm7, %xmm6
testq %rsi, %r10
seta %r8b
movzbq %r8b, %r8
orq $30, %rdi
movq %rdx, 72(%rbx)
movw %ax, 48(%rbx)
adcq %r10, %rax
popq %rsi
movb $0, %cl
rorq %cl, %rax
imulq %r11, %r15
negq %r9
movq %r15, 96(%rbx)
movq %rdi, 0(%rbx)
sbbq %r11, %r9
movl $275283416, %edx
andq $25, %r14
cmpq %rax, %rdi
setge %r8b
movzbq %r8b, %r8
movl 24(%rbx), %r12d
notq %r14
movq 24(%rbx), %rdx
andq $71, %r9
orq %rbx, %r9
subq %r8, %rcx
andq %r13, %r10
incq %r12
pxor %xmm5, %xmm11
adcq %rdx, %r11
subq $-58, %r13
imulq %rcx, %r11
andq $-55, %r12
andq %rsi, %r8
xorq %rax, %r13
movb $8, %cl
sarq %cl, %rax
movdqa %xmm3, %xmm15
sbbq %r13, %r14
movq 16(%rbx), %r9
addq $103, %r10
cmpq %r15, %rbx
setbe %r8b
movzbq %r8b, %r8
subq %r9, %r11
addq $-91, %r10
decq %r10
    movabsq $0xf0000000, %r15
    movq $0, (%r15)
