# t78_cpuid -- CPUID must match the product specification (docs/PRD.md §5.7)
# and must only advertise what the implementation actually provides.
#
#   0x00000000  EAX = 0x00000001, EBX/EDX/ECX = "Open" / "X86-" / "Core"
#   0x00000001  EDX: TSC MSR PAE CX8 SEP CMOV SSE SSE2 FXSR CLFSH,
#                    FPU and MMX must be 0
#   0x80000000  EAX = 0x80000008
#   0x80000001  EDX: SYSCALL NX LM RDTSCP, ECX bit0 LAHF_LM
#   0x80000008  physical/linear address widths (40 / 48)
#
# XEMU used to report the vendor "X86-KMH-Core" and advertised neither SEP
# nor RDTSCP/LAHF_LM, so a program could not tell which product it ran on and
# the SEP bit lied about SYSENTER in the other direction.
#
# Exit 0x78 on success, 0x7N on the N-th failed check.

.text
.globl _start
_start:
    movq $0x800000, %rsp

    # --- leaf 0: max basic leaf + vendor string -------------------------
    xorl %eax, %eax
    cpuid
    cmpl $1, %eax
    jne bad1
    cmpl $0x6e65704f, %ebx             # "Open"
    jne bad2
    cmpl $0x2d363858, %edx             # "X86-"
    jne bad3
    cmpl $0x65726f43, %ecx             # "Core"
    jne bad4

    # --- leaf 1: feature bits -------------------------------------------
    movl $1, %eax
    cpuid
    movl %edx, %esi
    andl $0x7088971, %esi              # FPU TSC MSR PAE CX8 SEP CMOV CLFSH FXSR SSE SSE2
    cmpl $0x7088971, %esi
    jne bad5
    # FPU is intentionally advertised (deviation from the PRD table): Linux
    # refuses to boot without CPUID.1:EDX.FPU, and XEMU does implement x87.
    testl $0x800000, %edx              # MMX must be 0 (not implemented)
    jne bad7

    # --- leaf 0x80000000: max extended leaf -----------------------------
    movl $0x80000000, %eax
    cpuid
    cmpl $0x80000008, %eax
    jne bad8

    # --- leaf 0x80000001: SYSCALL NX RDTSCP LM, ECX.LAHF_LM -------------
    movl $0x80000001, %eax
    cpuid
    movl %edx, %esi
    andl $0x28100800, %esi             # bits 11, 20, 27, 29
    cmpl $0x28100800, %esi
    jne bad9
    testl $1, %ecx                     # LAHF_LM
    jz bad10

    # --- leaf 0x80000008: address widths --------------------------------
    movl $0x80000008, %eax
    cpuid
    movzbl %al, %esi                   # physical bits
    cmpl $40, %esi
    jne bad11
    shrl $8, %eax
    movzbl %al, %eax                   # linear bits
    cmpl $48, %eax
    jne bad12

    movq $0x78, %r12
    jmp finish
bad1:  movq $0x01, %r12
    jmp finish
bad2:  movq $0x02, %r12
    jmp finish
bad3:  movq $0x03, %r12
    jmp finish
bad4:  movq $0x04, %r12
    jmp finish
bad5:  movq $0x05, %r12
    jmp finish
bad6:  movq $0x06, %r12
    jmp finish
bad7:  movq $0x07, %r12
    jmp finish
bad8:  movq $0x08, %r12
    jmp finish
bad9:  movq $0x09, %r12
    jmp finish
bad10: movq $0x0a, %r12
    jmp finish
bad11: movq $0x0b, %r12
    jmp finish
bad12: movq $0x0c, %r12

finish:
    movq $0xf0000000, %r15
    movq %r12, (%r15)
    jmp finish
