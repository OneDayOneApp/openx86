#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
"""T5 gate: drive the interactive busybox shell on the XEMU serial console.

The shell initramfs (``mkinitramfs.py --shell``) mounts /proc, /sys and /dev,
prints the banner ``XEMU-SHELL-READY``, and execs an interactive busybox
``sh`` as pid 1 on the kernel-provided console fds (``/dev/console``, which
is ttyS0 because of ``console=ttyS0``).

The test is a black-box serial-console driver::

    banner:   XEMU-SHELL-READY
    prompt:   / #
    commands: echo XEMU-ECHO-OK          ->  XEMU-ECHO-OK
              ls /                       ->  bin init proc ...
              cat /proc/cpuinfo          ->  processor : 0 / vendor_id : ...
    end:      poweroff -f                ->  reboot: System halted

``ls`` and ``cat`` are real fork+exec applets, so they also cover process
creation and /proc.  ``cat /proc/cpuinfo | head -3`` (the exact L4 wording) is
part of the default flow: a pipe between two forked applets works since
indirect CALL/JMP no longer clobber the #PF handler RIP when their operand
load faults.

Cursor-position queries (DSR)
-----------------------------
busybox ash's line editor emits ``ESC[6n`` right after a prompt and waits for
the terminal's cursor-position reply.  A *human* running this by hand gets
that reply from their terminal emulator; a bare pipe does not.  This harness
answers every query with ``ESC[1;1R`` (see ``Console.answer_dsr``), so typed
commands are never swallowed as a partial DSR reply.  If you drive the shell
with a different script, send that reply (or disable the line editor) too.

The first command is sent only after the banner and prompt are visible; the
8250 model's stdin poll is non-blocking now, so no input has to be preloaded
to keep the emulator from freezing.

Exit status is 0 only when every enabled check passes.  The complete serial
log is always written to ``build/linux/shell-test.log`` and printed.
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
PROMPT_RE = re.compile(rb"(?m)^(?:/|~) # ")
# busybox ls emits SGR colour sequences on a tty; compare on plain text.
ANSI_RE = re.compile(rb"\x1b\[[0-9;?]*[A-Za-z]")

# busybox ash's line editor asks the terminal for the cursor position right
# after a prompt (ESC[6n).  A real terminal answers ESC[row;colR; the harness
# does that so typed commands are not consumed as a partial DSR reply.
DSR_QUERY = b"\x1b[6n"
DSR_REPLY = b"\x1b[1;1R"


def plain(data):
    return ANSI_RE.sub(b"", data)

# Interactive commands typed at the serial console.  ``ls`` and ``cat`` need
# fork+exec; they work now that the emulator's fork/COW TLB bug is fixed.
COMMANDS = [
    (
        "echo XEMU-ECHO-OK",
        lambda s: re.search(rb"\r*\nXEMU-ECHO-OK\r*\n", s) is not None,
        "output line XEMU-ECHO-OK",
    ),
    (
        "ls /",
        lambda s: all(n in plain(s) for n in (b"bin", b"init", b"proc")),
        "listing with bin/init/proc",
    ),
    (
        "cat /proc/cpuinfo",
        lambda s: b"processor" in plain(s) and b"vendor_id" in plain(s),
        "processor + vendor_id",
    ),
]

# The exact acceptance pipeline from the original L4 wording.  It is not in
# the default flow yet: with the current emulator a pipe between two forked
# applets (`cat ... | head -3`) hangs before head can drain it, and a later
# prompt can then surface an oops with RIP=0.  Enable with --with-pipeline
# once that is fixed; `cat`/`head` individually work.
PIPELINE_COMMANDS = [
    (
        "cat /proc/cpuinfo | head -3",
        lambda s: b"processor" in plain(s) and
                  (b"vendor_id" in plain(s) or b"model name" in plain(s)),
        "processor + vendor_id/model name",
    ),
]

# Deterministic end states: `poweroff -f` prints "reboot: System halted";
# `exit` as pid 1 makes the kernel panic ("Attempted to kill init").  Either
# counts as the session having ended.
END_MARKERS = (
    b"Attempted to kill init",
    b"reboot: System halted",
    b"reboot: Power down",
    b"xemu: halted",
)


class Console:
    def __init__(self, proc):
        self.proc = proc
        self.log = bytearray()
        self.dsr_answered = 0

    def drain(self, timeout=0.1):
        """Read whatever is available within `timeout`; return False on EOF."""
        r, _, _ = select.select([self.proc.stdout], [], [], timeout)
        if not r:
            return True
        chunk = os.read(self.proc.stdout.fileno(), 65536)
        if not chunk:
            return False
        self.log += chunk
        self.answer_dsr()
        return True

    def answer_dsr(self):
        """Reply to every new ESC[6n cursor-position query from the shell."""
        n = bytes(self.log).count(DSR_QUERY)
        while self.dsr_answered < n:
            try:
                self.proc.stdin.write(DSR_REPLY)
                self.proc.stdin.flush()
            except (BrokenPipeError, OSError):
                return
            self.dsr_answered += 1

    def wait_for(self, predicate, deadline, label, keepalive=False):
        """Pump the serial output until predicate(log) or deadline.

        With keepalive=True a newline is written to the guest every 0.5 s
        while waiting.  The 8250 model performs a *blocking* read(2) on host
        stdin whenever the guest has RX interrupts enabled; during the
        userspace-boot window the pipe can otherwise go empty and freeze the
        emulator mid-banner.  Empty command lines are harmless to the shell.
        """
        next_keepalive = time.time() + 0.5
        while time.time() < deadline:
            if predicate(bytes(self.log)):
                return True, None
            if not self.drain(0.2):
                return predicate(bytes(self.log)), "emulator closed stdout"
            if self.proc.poll() is not None:
                while self.drain(0):
                    pass
                ok = predicate(bytes(self.log))
                return ok, ("emulator exited with %s" % self.proc.returncode)
            if keepalive and time.time() >= next_keepalive:
                try:
                    self.proc.stdin.write(b"\n")
                    self.proc.stdin.flush()
                except (BrokenPipeError, OSError):
                    pass
                next_keepalive = time.time() + 0.5
        ok = predicate(bytes(self.log))
        return ok, (None if ok else "timed out waiting for " + label)

    def settle(self, seconds=0.25):
        """Let pending prompt/DSR output arrive and answer it before typing."""
        end = time.time() + seconds
        while time.time() < end:
            self.drain(0.1)
        self.answer_dsr()

    def send(self, line):
        self.proc.stdin.write(line.encode() + b"\n")
        self.proc.stdin.flush()


def main():
    ap = argparse.ArgumentParser(description="interactive busybox shell test")
    ap.add_argument("--xemu", default=XEMU)
    ap.add_argument("--kernel", default=KERNEL)
    ap.add_argument("--initrd", default=INITRD)
    ap.add_argument("--cmdline", default="console=ttyS0")
    ap.add_argument("--max-steps", type=int, default=120_000_000,
                    help="emulator instruction cap (default %(default)s)")
    ap.add_argument("--banner-timeout", type=float, default=240.0)
    ap.add_argument("--command-timeout", type=float, default=90.0)
    ap.add_argument("--halt-timeout", type=float, default=60.0)
    ap.add_argument("--with-pipeline", action="store_true", default=True,
                    help="also require the exact `cat /proc/cpuinfo | head -3` "
                         "pipeline (currently hangs in XEMU; cat/head each work)")
    # Kept so existing invocations do not break; ls/cat are now default.
    ap.add_argument("--with-forked-commands", action="store_true",
                    help=argparse.SUPPRESS)
    ap.add_argument("--log", default=LOG)
    ap.add_argument("--quiet", action="store_true")
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
        # ---- 1. banner ----------------------------------------------------
        dl = start + args.banner_timeout
        ok, why = con.wait_for(lambda s: BANNER in s, dl, "banner")
        record(ok, "banner", BANNER.decode() if ok else (why or "banner missing"))
        if not ok:
            raise SystemExit(0)

        # ---- 2. interactive prompt ---------------------------------------
        dl = time.time() + args.command_timeout
        ok, why = con.wait_for(lambda s: PROMPT_RE.search(s) is not None, dl,
                               "shell prompt")
        record(ok, "prompt", "'/ #'" if ok else (why or "prompt missing"))

        # ---- 3. commands typed over the serial console --------------------
        commands = list(COMMANDS)
        if args.with_pipeline:
            commands += PIPELINE_COMMANDS
        for i, (line, check, desc) in enumerate(commands):
            con.settle()
            before = len(con.log)
            con.send(line)
            dl = min(time.time() + args.command_timeout,
                     start + args.banner_timeout + (i + 1) * args.command_timeout)
            ok, why = con.wait_for(
                lambda s, before=before, check=check: check(plain(s[before:])),
                dl, desc)
            record(ok, line, desc if ok else (why or "expected output missing"))

        # ---- 4. end the session ------------------------------------------
        con.settle()
        con.send("poweroff -f")
        end_pred = lambda s: any(m in s for m in END_MARKERS)
        dl = time.time() + min(args.halt_timeout, 20.0)
        ok, _ = con.wait_for(end_pred, dl, "poweroff")
        if not ok:
            # `exit` is a builtin; pid 1 exiting panics the kernel, which is
            # still a deterministic end state.
            con.send("exit")
            dl = time.time() + args.halt_timeout
            ok, why = con.wait_for(end_pred, dl, "exit")
        record(ok, "end", "kernel stopped / pid 1 exited" if ok
               else (why or "no end marker"))
    finally:
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
        mode = "with-pipeline" if args.with_pipeline else "default"
        print(f"\nshell: {'PASS' if failure is None else 'FAIL'}  "
              f"{mode}  {elapsed:.1f}s  log={os.path.relpath(logpath, ROOT)}")
        if failure is not None:
            print(f"first failure: {failure}")
            if b"segfault at 1 ip 0000000000000001" in log:
                print("note: forked child returned to RIP=1 (known emulator bug); "
                      "see the module docstring")
            insn = re.search(rb"after (\d+) instructions", log)
            if insn:
                print(f"instructions executed: {insn.group(1).decode()}")
            print(f"console log written to {logpath}")
        sys.exit(0 if failure is None else 1)


if __name__ == "__main__":
    main()
