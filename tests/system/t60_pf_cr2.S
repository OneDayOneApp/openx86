# t60_pf_cr2 -- CR2 must hold the faulting linear address for read, write
# and instruction-fetch page faults.
#
# Bottom-up level: one architectural mechanism (#PF/CR2) in isolation.
#
# The #PF handler installs the missing 2 MiB page and iretq retries the
# faulting instruction, which is exactly the pattern Linux's early boot
# handler (do_early_exception -> early_make_pgtable) relies on. If the access
# is retried correctly and CR2 was right, every probe completes.
#
# Exit 0x60 on success, 0x6N on the N-th failed check.

.text
.globl _start
_start:
    movq $0x800000, %rsp

    # ---- page tables at 0x500000 ---------------------------------------
    # PML4[0] -> PDPT 0x501000, PDPT[0] -> PD 0x502000,
    # PD[0..4] identity 2 MiB pages covering 0..10 MiB (code, stack, tables)
    movq $0x500000, %rdi
    movq $0, 0x500000
    movq $0x501003, 0x500000          # PML4[0]
    movq $0, 0x501000
    movq $0x502003, 0x501000          # PDPT[0]
    movq $0, 0x502000
    movq $0x00000083, 0x502000        # PD[0]  0..2 MiB
    movq $0x00200083, 0x502008        # PD[1]  2..4 MiB
    movq $0x00400083, 0x502010        # PD[2]  4..6 MiB (code + tables + IDT)
    movq $0x00600083, 0x502018        # PD[3]  6..8 MiB
    movq $0x00800083, 0x502020        # PD[4]  8..10 MiB (stack)
    movq $0, 0x502028                 # PD[5]  absent -> probe 1 (10 MiB)
    movq $0, 0x502030                 # PD[6]  absent -> probe 2 (12 MiB)
    movq $0, 0x502038                 # PD[7]  absent -> probe 3 (14 MiB)

    # The halt MMIO lives at 0xF0000000 (PDPT index 3), so it needs its own PD.
    # Map it up front; otherwise the exit store faults and the handler cannot
    # build it (the handler only patches the PDPT[0] table).
    movq $0x503003, 0x501018          # PDPT[3] -> PD 0x503000
    movq $0, 0x503000
    movabsq $0xF0000083, %rax         # present|rw|ps page for 0xF0000000
    movq %rax, 0x503C00               # PD[384]

    # ---- IDT at 0x5A0000, vector 14 -> pf_handler ----------------------
    movq $0x5A0000, %rdi
    leaq pf_handler(%rip), %rax
    movq %rax, %rcx
    andq $0xffff, %rcx
    movq %rax, %rdx
    shrq $16, %rdx
    andq $0xffff, %rdx
    shlq $48, %rdx
    orq %rdx, %rcx                    # offset[15:0] | offset[31:16]<<48
    movq $0x8e0000080000, %rdx        # type_attr=0x8e, sel=0x08
    orq %rdx, %rcx
    movq %rcx, 0x5A0000 + 14*16       # gate for vector 14
    movq $0, 0x5A0000 + 14*16 + 8
    movw $0xFF, 0x5A8000              # IDTR limit: 16 vectors
    movq $0x5A0000, 0x5A8002          # IDTR base
    movq $0x5A8000, %rax
    lidt (%rax)

    # ---- probe payloads, written through the identity map -------------
    movq $0xA00000, %rax
    movq $0x1111, (%rax)              # probe 1 data
    movq $0xC00000, %rax
    movq $0x2222, (%rax)              # probe 2 data
    # probe 3 is executed: put "jmp *%r11" at 0xE00000
    movq $0xE00000, %rax
    movl $0x00E3FF41, (%rax)          # 41 FF E3 = jmp *%r11

    # ---- turn on paging ------------------------------------------------
    movq %cr4, %rax
    orq $0x20, %rax                   # PAE
    movq %rax, %cr4
    movq $0x500000, %rax
    movq %rax, %cr3
    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax                  # LME
    wrmsr
    movq %cr0, %rax
    movabsq $0x80000001, %rdx         # PG | PE
    orq %rdx, %rax
    movq %rax, %cr0

    # ---- probe 1: read from an unmapped page ---------------------------
    movq $0xA00000, %rax
    movq (%rax), %rbx
    cmpq $0x1111, %rbx
    jne bad1

    # ---- probe 2: write to an unmapped page ----------------------------
    movq $0xC00000, %rax
    movq $0x3333, (%rax)

    # ---- probe 3: instruction fetch from an unmapped page --------------
    leaq after_fetch(%rip), %r11
    movq $0xE00000, %rax
    jmp *%rax
after_fetch:

    # ---- all three faults happened, with the right CR2 -----------------
    movq $0x504018, %rcx
    cmpq $3, (%rcx)
    jne bad4
    movq $0x504000, %rsi
    cmpq $0xA00000, 0(%rsi)
    jne bad5
    cmpq $0xC00000, 8(%rsi)
    jne bad6
    cmpq $0xE00000, 16(%rsi)
    jne bad7

    movq $0x60, %r12
    jmp finish

bad1: movq $0x61, %r12
    jmp finish
bad4: movq $0x64, %r12
    jmp finish
bad5: movq $0x65, %r12
    jmp finish
bad6: movq $0x66, %r12
    jmp finish
bad7: movq $0x67, %r12
    jmp finish

# Build the missing 2 MiB page for CR2, record CR2, and return so the
# faulting instruction is retried by the CPU.
pf_handler:
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %r8

    movq %cr2, %rax
    movq $0x504000, %rsi
    movq $0x504018, %rcx
    movq (%rcx), %rdx
    movq %rax, (%rsi,%rdx,8)          # record CR2
    incq %rdx
    movq %rdx, (%rcx)

    movq %rax, %rdx
    shrq $21, %rdx                    # PD index
    movq %rdx, %r8
    shlq $21, %r8
    orq $0x83, %r8                    # present | rw | ps
    movq $0x502000, %rcx
    movq %r8, (%rcx,%rdx,8)
    invlpg (%rax)

    popq %r8
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rax
    addq $8, %rsp                     # drop the hardware error code
    iretq

finish:
    movq $0xf0000000, %r15
    movq %r12, (%r15)
    jmp finish
