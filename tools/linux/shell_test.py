#!/usr/bin/env python3
"""T5 gate: drive the interactive busybox shell on the XEMU serial console.

The shell initramfs (``mkinitramfs.py --shell``) mounts proc/sys/dev, prints
the banner ``XEMU-SHELL-READY`` and execs an interactive busybox ``sh`` with
fd 0/1/2 on ``/dev/ttyS0``.  This script

  1. starts ``build/xemu-linux`` with its stdin on a pipe,
  2. waits (incrementally, with a hard deadline) for the banner,
  3. types real commands one by one and checks their *output*,
  4. asks the guest to power off.

It is deliberately a black-box console test: nothing here looks inside the
emulator.  The captured serial log is always written to
``build/linux/shell-test.log`` (and printed), so a failure can be inspected
byte for byte.

Exit status is 0 only when every command produced its expected output.
"""

import argparse
import os
import re
import select
import signal
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
XEMU = os.path.join(ROOT, "build", "xemu-linux")
KERNEL = os.path.join(ROOT, "build", "linux", "vmlinux")
INITRD = os.path.join(ROOT, "build", "linux", "initramfs-shell.cpio.gz")
LOG = os.path.join(ROOT, "build", "linux", "shell-test.log")

BANNER = b"XEMU-SHELL-READY"

# Commands and the output each one must produce.  The check runs only on the
# bytes received *after* the command was written, and must not match the
# terminal echo of the command line itself (the line disciplines echoes it).
#
# `cat /proc/cpuinfo | head -3` genuinely exercises a pipeline (two forks),
# which is also a good test of process creation under the emulator.
COMMANDS = [
    (
        "echo HELLO-FROM-GUEST",
        lambda s: re.search(rb"\r?\nHELLO-FROM-GUEST\r?\n", s) is not None,
        "output line HELLO-FROM-GUEST",
    ),
    (
        "ls /",
        lambda s: all(re.search(rb"(?<![A-Za-z0-9_.-])" + n + rb"(?![A-Za-z0-9_.-])", s)
                      for n in (b"bin", b"init", b"proc")),
        "listing with bin/init/proc",
    ),
    (
        "cat /proc/cpuinfo | head -3",
        lambda s: b"processor" in s and (b"vendor_id" in s or b"model name" in s),
        "processor + vendor_id/model name",
    ),
]

HALT_MARKERS = (
    b"reboot: System halted",
    b"reboot: Power down",
    b"xemu: halted",
)


class Console:
    def __init__(self, proc):
        self.proc = proc
        self.log = bytearray()

    def drain(self, timeout=0.1):
        """Read whatever is available within `timeout`; return False on EOF."""
        r, _, _ = select.select([self.proc.stdout], [], [], timeout)
        if not r:
            return True
        chunk = os.read(self.proc.stdout.fileno(), 65536)
        if not chunk:
            return False
        self.log += chunk
        return True

    def wait_for(self, predicate, deadline, label, quiet=False):
        """Pump the serial output until predicate(log) or deadline."""
        while time.time() < deadline:
            if predicate(bytes(self.log)):
                return True, None
            if not self.drain(0.2):
                # EOF: process closed stdout, give the predicate one last look.
                return predicate(bytes(self.log)), "emulator closed stdout"
            if self.proc.poll() is not None:
                # Drain the rest, then stop.
                while self.drain(0):
                    pass
                ok = predicate(bytes(self.log))
                return ok, ("emulator exited with %s" % self.proc.returncode)
        ok = predicate(bytes(self.log))
        return ok, (None if ok else "timed out waiting for " + label)

    def send(self, line):
        self.proc.stdin.write(line.encode() + b"\n")
        self.proc.stdin.flush()


def main():
    ap = argparse.ArgumentParser(description="interactive busybox shell test")
    ap.add_argument("--xemu", default=XEMU)
    ap.add_argument("--kernel", default=KERNEL)
    ap.add_argument("--initrd", default=INITRD)
    ap.add_argument("--cmdline", default="console=ttyS0")
    ap.add_argument("--max-steps", type=int, default=200_000_000,
                    help="emulator instruction cap (default %(default)s)")
    ap.add_argument("--banner-timeout", type=float, default=240.0)
    ap.add_argument("--command-timeout", type=float, default=90.0)
    ap.add_argument("--halt-timeout", type=float, default=45.0)
    ap.add_argument("--log", default=LOG)
    ap.add_argument("--quiet", action="store_true",
                    help="do not echo the serial log live")
    args = ap.parse_args()

    xemu = args.xemu if os.path.isabs(args.xemu) else os.path.join(ROOT, args.xemu)
    if not os.path.exists(xemu):
        sys.exit(f"{args.xemu} missing; run `make xemu-linux-runner` first")
    for p, what in ((args.kernel, "kernel"), (args.initrd, "shell initramfs")):
        if not os.path.exists(p):
            hint = "run `make build/linux/initramfs-shell.cpio.gz`" \
                if what == "shell initramfs" else "run tools/linux/build_kernel.sh"
            sys.exit(f"missing {what}: {p} ({hint})")

    cmd = [
        xemu,
        "--kernel", args.kernel,
        "--cmdline=" + args.cmdline,
        "--initrd", args.initrd,
        "-b", "--max-steps=%d" % args.max_steps,
    ]
    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        bufsize=0,
        start_new_session=True,
    )
    con = Console(proc)
    start = time.time()

    results = []
    failure = None

    def record(ok, name, detail):
        nonlocal failure
        results.append((ok, name, detail))
        if not ok and failure is None:
            failure = f"{name}: {detail}"

    try:
        # ---- 1. wait for the distinctive userspace banner -----------------
        dl = start + args.banner_timeout
        ok, why = con.wait_for(lambda s: BANNER in s, dl, "banner", args.quiet)
        record(ok, "banner", BANNER.decode() if ok else (why or "banner missing"))
        if not ok:
            raise SystemExit(0)

        # Give the shell a moment to open /dev/ttyS0 and enable RX before the
        # first byte is pushed into the pipe.  Not required for correctness
        # (the byte is buffered), but it keeps the log easy to read.
        time.sleep(0.5)

        # ---- 2. drive the commands ---------------------------------------
        for i, (line, check, desc) in enumerate(COMMANDS):
            before = len(con.log)
            con.send(line)
            dl = min(start + args.banner_timeout + (i + 1) * args.command_timeout,
                     time.time() + args.command_timeout)
            ok, why = con.wait_for(
                lambda s, before=before, check=check: check(s[before:]),
                dl, desc, args.quiet)
            record(ok, line, desc if ok else (why or "expected output missing"))

        # ---- 3. clean shutdown -------------------------------------------
        con.send("poweroff -f")
        dl = time.time() + args.halt_timeout
        ok, why = con.wait_for(
            lambda s: any(m in s for m in HALT_MARKERS), dl, "halt", args.quiet)
        record(ok, "poweroff -f", "kernel halted" if ok else (why or "no halt marker"))
    finally:
        # Stop a guest that is still sitting in HLT or waiting for input.
        if proc.poll() is None:
            try:
                os.killpg(proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(proc.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                proc.wait()
        # Final drain: reading the pipe is safe now that the child is gone.
        try:
            while con.drain(0):
                pass
        except OSError:
            pass
        elapsed = time.time() - start

        log = bytes(con.log)
        logpath = args.log if os.path.isabs(args.log) else os.path.join(ROOT, args.log)
        os.makedirs(os.path.dirname(logpath), exist_ok=True)
        with open(logpath, "wb") as f:
            f.write(log)

        print()
        if not args.quiet:
            print("---- serial console ----")
            sys.stdout.write(log.decode("utf-8", "replace"))
            print("---- end serial console ----")
        for ok, name, detail in results:
            print(f"{'PASS' if ok else 'FAIL'}  {name:<28} {detail}")
        print(f"\nshell: {'PASS' if failure is None else 'FAIL'}  "
              f"{elapsed:.1f}s  log={os.path.relpath(logpath, ROOT)}")
        if failure is not None:
            print(f"first failure: {failure}")
            insn = re.search(rb"after (\d+) instructions", log)
            if insn:
                print(f"instructions executed: {insn.group(1).decode()}")
            print(f"console log written to {logpath}")
        sys.exit(0 if failure is None else 1)


if __name__ == "__main__":
    main()
