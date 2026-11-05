# t85_pci -- PCI mechanism #1 configuration space (L4).
#
# The stub implements CONFIG_ADDRESS at 0xCF8 and CONFIG_DATA at
# 0xCFC..0xCFF with a single 440FX host bridge at bus 0 / dev 0 / fn 0.
# This test pins down:
#   * the enable bit (bit 31): clear => CONFIG_DATA reads all ones and
#     writes are ignored
#   * CONFIG_ADDRESS readback with bits 1:0 hardwired to 0
#   * vendor/device ID 0x8086:0x1237 for the host bridge
#   * absent devices (bus 0, dev 1) read all ones
#   * byte/word lanes at 0xCFC..0xCFF and the unaligned dword behaviour:
#     a dword at 0xCFE returns the upper half of CONFIG_DATA in its low half
#     and 0xFFFF for the bytes past the CONFIG_DATA window
#   * the command register is R/W (masked to 0x07FF) while the status half
#     is read-only
#   * vendor ID writes are ignored
#   * BAR0 sizing: the BAR is unimplemented, so the all-ones probe reads
#     back the size mask 0
#
# Exit 0x85 on success. A failure reports 0x8N for the check labelled N
# (labels 1..4 and 6..15; 5 is skipped so no failure aliases 0x85).

.text
.globl _start
_start:
    movq $0x800000, %rsp

    # --- 1: bit31 clear -> CONFIG_DATA reads all ones --------------------
    xorl %eax, %eax
    movw $0xcf8, %dx
    outl %eax, %dx                 # CONFIG_ADDRESS = 0 (disabled)
    movw $0xcfc, %dx
    inl %dx, %eax
    cmpl $0xffffffff, %eax
    jne bad1
    movl $0x11223344, %eax         # a write while disabled is ignored
    outl %eax, %dx

    # --- 2: CONFIG_ADDRESS readback, bits 1:0 forced to 0 ----------------
    movw $0xcf8, %dx
    movl $0x80000001, %eax
    outl %eax, %dx
    inl %dx, %eax
    cmpl $0x80000000, %eax
    jne bad2
    # A byte write to the enable lane clears bit31...
    movw $0xcfb, %dx
    xorb %al, %al
    outb %al, %dx
    movw $0xcfc, %dx
    inl %dx, %eax
    cmpl $0xffffffff, %eax
    jne bad2
    # ...and writing it back re-enables bus0/dev0/fn0/reg0.
    movw $0xcfb, %dx
    movb $0x80, %al
    outb %al, %dx

    # --- 3: bus0/dev0/fn0/reg0 = vendor:device 0x8086:0x1237 -------------
    movw $0xcfc, %dx
    inl %dx, %eax
    cmpl $0x12378086, %eax
    jne bad3

    # --- 4: absent bus0/dev1/fn0 reads all ones --------------------------
    movw $0xcf8, %dx
    movl $0x80000800, %eax
    outl %eax, %dx
    movw $0xcfc, %dx
    inl %dx, %eax
    cmpl $0xffffffff, %eax
    jne bad4

    # Select the host bridge's vendor/device dword again for lane checks.
    movw $0xcf8, %dx
    movl $0x80000000, %eax
    outl %eax, %dx

    # --- 6: byte lanes 0 and 1 (vendor ID low/high) ----------------------
    movw $0xcfc, %dx
    inb %dx, %al
    cmpb $0x86, %al
    jne bad6
    movw $0xcfd, %dx
    inb %dx, %al
    cmpb $0x80, %al
    jne bad6

    # --- 7: byte lanes 2 and 3 (device ID low/high) ----------------------
    movw $0xcfe, %dx
    inb %dx, %al
    cmpb $0x37, %al
    jne bad7
    movw $0xcff, %dx
    inb %dx, %al
    cmpb $0x12, %al
    jne bad7

    # --- 8: word at 0xCFC = 0x8086 ---------------------------------------
    movw $0xcfc, %dx
    inw %dx, %ax
    cmpw $0x8086, %ax
    jne bad8

    # --- 9: word at 0xCFE = 0x1237 ---------------------------------------
    movw $0xcfe, %dx
    inw %dx, %ax
    cmpw $0x1237, %ax
    jne bad9

    # --- 10: unaligned dword at 0xCFE: upper half of CONFIG_DATA low,
    #         the bytes past 0xCFF read as 0xFF in the high half -----------
    movw $0xcfe, %dx
    inl %dx, %eax
    cmpl $0xffff1237, %eax
    jne bad10

    # --- 11: unaligned dword at 0xCFD: bytes 2,3 of lane 0.. -----------
    #         0x80 0x37 0x12 then 0xFF past the window = 0xFF123780
    movw $0xcfd, %dx
    inl %dx, %eax
    cmpl $0xff123780, %eax
    jne bad11

    # --- 12: command is R/W at 0x04, status reads 0x0200 -----------------
    movw $0xcf8, %dx
    movl $0x80000004, %eax
    outl %eax, %dx
    movw $0xcfc, %dx
    movw $0x0007, %ax              # 16-bit OUT to the command register
    outw %ax, %dx
    inl %dx, %eax
    cmpl $0x02000007, %eax
    jne bad12

    # --- 13: command reserved bits masked, status half read-only ---------
    movl $0xffffffff, %eax
    outl %eax, %dx
    inl %dx, %eax
    cmpl $0x020007ff, %eax
    jne bad13

    # --- 14: vendor/device dword (and its low byte) are read-only --------
    movw $0xcf8, %dx
    movl $0x80000000, %eax
    outl %eax, %dx
    movw $0xcfc, %dx
    movl $0xffffffff, %eax
    outl %eax, %dx
    inl %dx, %eax
    cmpl $0x12378086, %eax
    jne bad14
    movb $0xaa, %al
    outb %al, %dx
    inb %dx, %al
    cmpb $0x86, %al
    jne bad14

    # --- 15: BAR0 probe: reads 0, all ones reads back size mask 0 --------
    movw $0xcf8, %dx
    movl $0x80000010, %eax
    outl %eax, %dx
    movw $0xcfc, %dx
    inl %dx, %eax
    cmpl $0, %eax
    jne bad15
    movl $0xffffffff, %eax
    outl %eax, %dx
    inl %dx, %eax
    cmpl $0, %eax
    jne bad15
    xorl %eax, %eax
    outl %eax, %dx
    inl %dx, %eax
    cmpl $0, %eax
    jne bad15

    movq $0x85, %r12
    jmp finish

bad1:  movq $0x81, %r12
    jmp finish
bad2:  movq $0x82, %r12
    jmp finish
bad3:  movq $0x83, %r12
    jmp finish
bad4:  movq $0x84, %r12
    jmp finish
bad6:  movq $0x86, %r12
    jmp finish
bad7:  movq $0x87, %r12
    jmp finish
bad8:  movq $0x88, %r12
    jmp finish
bad9:  movq $0x89, %r12
    jmp finish
bad10: movq $0x8a, %r12
    jmp finish
bad11: movq $0x8b, %r12
    jmp finish
bad12: movq $0x8c, %r12
    jmp finish
bad13: movq $0x8d, %r12
    jmp finish
bad14: movq $0x8e, %r12
    jmp finish
bad15: movq $0x8f, %r12

finish:
    movq $0xf0000000, %r15
    movq %r12, (%r15)
    jmp finish
