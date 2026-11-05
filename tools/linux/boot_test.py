#!/usr/bin/env python3
"""T4 gate: boot real Linux under XEMU and check the serial console.

This is the top of the test pyramid in xemu/docs/linux-testplan.md: every
lower layer (ISA diff, device tests, freestanding kernels) exists to make this
pass. The test drives the standalone runner exactly like a user would and
matches the kernel's serial output against a list of required milestones.

Exit status is 0 only when every milestone appeared in order.
"""

import argparse
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
XEMU = os.path.join(ROOT, "build", "xemu-linux")

# (label, substring) checked in order against the serial console.
MILESTONES = [
    ("kernel banner",       "Linux version"),
    ("boot protocol",       "BIOS-provided physical RAM map"),
    ("command line",        "ommand line: "),
    ("memory accounting",   "Memory: "),
    ("serial console",      "printk: console [ttyS0] enabled"),
    ("interrupt setup",     "APIC: Keep in PIC mode(8259)"),
    ("initramfs unpacked",  "Unpacking initramfs..."),
    ("virtual memory",      "Write protecting the kernel read-only data"),
    ("userspace entered",   "Run /init as init process"),
    ("init stdout",         "XEMU: /init running as pid 1"),
    ("init completed",      "XEMU: initramfs mounted, shutting down"),
]

# The kernel halts or panics after init finishes; either is a clean end state.
END_STATES = [
    "reboot: System halted",
    "Kernel panic - not syncing",   # init exiting as pid 1 is expected
    "xemu: halted",
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--xemu", default=XEMU)
    ap.add_argument("--kernel", default=os.path.join(ROOT, "build", "linux", "vmlinux"))
    ap.add_argument("--initrd", default=os.path.join(ROOT, "build", "linux", "initramfs.cpio.gz"))
    ap.add_argument("--cmdline", default="console=ttyS0")
    ap.add_argument("--max-steps", type=int, default=30_000_000)
    ap.add_argument("--timeout", type=int, default=900)
    ap.add_argument("--log", default=os.path.join(ROOT, "build", "linux", "xemu-boot-test.log"))
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    xemu_bin = os.path.join(ROOT, args.xemu) if not os.path.isabs(args.xemu) else args.xemu
    if not os.path.exists(xemu_bin):
        sys.exit(f"{args.xemu} missing; run `make xemu-linux-runner` first")
    for p in (args.kernel, args.initrd):
        if not os.path.exists(p):
            sys.exit(f"missing {p}; run `tools/linux/build_kernel.sh` and `tools/linux/mkinitramfs.py`")

    cmd = [xemu_bin, "--kernel", args.kernel, f"--cmdline={args.cmdline}",
           "--initrd", args.initrd, "-b", f"--max-steps={args.max_steps}"]
    t0 = time.time()
    try:
        r = subprocess.run(cmd, capture_output=True, timeout=args.timeout)
        out = r.stdout.decode("utf-8", "replace")
        err = r.stderr.decode("utf-8", "replace")
        rc = r.returncode
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b"").decode("utf-8", "replace")
        err = (e.stderr or b"").decode("utf-8", "replace")
        rc = None
    elapsed = time.time() - t0

    log = out + err
    os.makedirs(os.path.dirname(args.log), exist_ok=True)
    with open(args.log, "w") as f:
        f.write(out)
        f.write("\n---- stderr ----\n")
        f.write(err)

    pos = 0
    results = []
    ok = True
    for label, needle in MILESTONES:
        idx = log.find(needle, pos)
        if idx < 0:
            results.append((False, label, needle))
            ok = False
            break
        results.append((True, label, needle))
        pos = idx + len(needle)

    ended = any(s in log for s in END_STATES)

    for good, label, needle in results:
        print(f"{'PASS' if good else 'FAIL'}  {label:<22} {needle!r}")
    print(f"{'PASS' if ended else 'WARN'}  {'clean end state':<22} "
          f"({', '.join(s for s in END_STATES if s in log) or 'not reached'})")

    insn = re.search(r"after (\d+) instructions", log)
    mips = (int(insn.group(1)) / elapsed / 1e6) if (insn and elapsed > 0) else 0.0
    print(f"\nboot: {'PASS' if (ok and ended) else 'FAIL'}  "
          f"{elapsed:.1f}s  {insn.group(1) if insn else '?'} instructions  "
          f"~{mips:.2f} MIPS  log={os.path.relpath(args.log, ROOT)}")
    if args.verbose or not ok:
        missing = [l for g, l, _ in results if not g]
        if missing:
            print(f"first missing milestone: {missing[0]}")
        tail = log.strip().splitlines()[-15:]
        print("---- last serial lines ----")
        print("\n".join(tail))
    return 0 if (ok and ended) else 1


if __name__ == "__main__":
    sys.exit(main())
