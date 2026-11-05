#!/usr/bin/env python3
"""Run XEMU-only system tests (CPL3/TSS/timer IRQ) on the standalone runner.

These features are not implemented in the RTL, so the oracle is the expected
exit code written to the halt MMIO by each test.
"""

import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
XEMU = os.path.join(ROOT, "build", "xemu")
BUILD = os.path.join(ROOT, "build", "sys")

# test -> expected halt exit code
CASES = [
    # P3: system mode
    ("t40_cpl3", 0x7a),
    ("t41_timer", 0x07),
    # L0: port I/O + devices
    ("t50_uart", 0x50),
    ("t51_pit", 0x60),
    ("t52_portio", 0x70),
    # L1: paging + exception mechanisms (bottom-up)
    ("t60_pf_cr2", 0x60),
    ("t61_early_idt", 0x61),
    # L3: memory bit-ops must advance the element address (Bochs-verified
    # semantics; Unicorn disagrees, so this is a XEMU-only oracle test).
    ("t63_bt_mem", 0x63),
    # L3: 64-bit pseudo-descriptors (SGDT/SIDT) keep the full 64-bit base.
    ("t64_sgdt_sidt", 0x64),
    # L3: paging permissions / A-D / reserved bits (Intel SDM error code).
    ("t70_us_violation", 0x70),
    ("t71_wp_violation", 0x71),
    ("t72_ad_bits", 0x72),
    ("t73_rsvd", 0x73),
    ("t74_user_ok", 0x74),
    # L3: a faulting stack push must leave CALL/RET atomically un-executed.
    ("t75_call_fault", 0x75),
    # L3: a CR3 load flushes the TLB even when the value is unchanged (Linux
    # flushes this way after fork write-protects the parent's COW pages); a
    # stale writable translation must not bypass the new read-only PTE.
    ("t76_cr3_flush", 0x76),
    # L3: an indirect JMP whose memory operand faults must leave the #PF
    # handler RIP intact (the load's 0 must not be written to RIP).
    ("t77_jmp_fault", 0x77),
    # CPUID must match docs/PRD.md 5.7 (vendor "OpenX86-Core" + feature bits).
    ("t78_cpuid", 0x78),
    # SYSENTER/SYSEXIT must work, since leaf 1 advertises SEP.
    ("t79_sysenter", 0x79),
    # L3 exceptions: #GP/#SS/#NP/#DF+IST and the INT n DPL check.
    ("t80_gp_noncanonical", 0x80),
    ("t81_gp_priv", 0x81),
    ("t82_ss_np", 0x82),
    ("t83_df_ist", 0x83),
    ("t84_intn_dpl", 0x84),
    # L4: PCI config space stub (0xCF8/0xCFC).
    ("t85_pci", 0x85),
    # x87 register forms (D8..DF with mod == 3): FLD/FXCH/FCHS/FABS/FTST/FXAM,
    # the constant block, the FCMOVcc/FCOMI family and the "P" arithmetic
    # forms whose destination is ST(1).
    ("t86_x87_regform", 0x86),
]


def assemble(name):
    src = os.path.join(ROOT, "tests", "system", name + ".S")
    obj = os.path.join(BUILD, name + ".o")
    binp = os.path.join(BUILD, name + ".bin")
    os.makedirs(BUILD, exist_ok=True)
    subprocess.run(["clang", "-target", "x86_64-unknown-none-elf", "-c", src, "-o", obj],
                   check=True)
    subprocess.run(["llvm-objcopy", "-O", "binary", obj, binp], check=True)
    return binp


def main():
    passed, failed = 0, []
    for name, want in CASES:
        binp = assemble(name)
        r = subprocess.run([XEMU, "-b", "-i", binp], capture_output=True, text=True,
                           timeout=300)
        out = r.stdout + r.stderr
        got = r.returncode
        if got == want:
            passed += 1
            line = [l for l in out.splitlines() if l.startswith("xemu: halted")]
            print(f"PASS {name}: exit=0x{got:02x} ({line[-1] if line else ''})")
        else:
            failed.append(name)
            print(f"FAIL {name}: exit=0x{got:02x} want 0x{want:02x}")
            for l in out.strip().splitlines()[-4:]:
                print("  " + l)
    print(f"\nxemu sys: {passed} passed, {len(failed)} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
