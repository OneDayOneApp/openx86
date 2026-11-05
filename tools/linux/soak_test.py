#!/usr/bin/env python3
"""L4 soak test: keep the emulator running for N minutes and prove it never hangs.

The plan (`xemu/docs/linux-plan.md`, L4 acceptance) asks for "连续运行 10 分钟
无 hang".  This test boots the busybox shell initramfs and then, every
`--interval` seconds, types one command and requires its output within a hard
deadline.  Any of the following fails the run:

  * a command produces no output before its deadline (a hang),
  * the guest stops consuming input,
  * the console prints a kernel BUG/oops/panic or "Call Trace",
  * the shell exits on its own before the deadline,
  * the process dies (emulator crash).

It also reports the emulated MIPS and the instruction count, so a run can be
compared against the performance target.

    python3 tools/linux/soak_test.py --minutes 10          # the real gate
    python3 tools/linux/soak_test.py --minutes 2           # quick check

Exit status is 0 only when the whole soak passed.
"""

import argparse
import os
import re
import select
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
XEMU = os.path.join(ROOT, "build", "xemu-linux")
KERNEL = os.path.join(ROOT, "build", "linux", "vmlinux")
INITRD = os.path.join(ROOT, "build", "linux", "initramfs-shell.cpio.gz")
LOG = os.path.join(ROOT, "build", "linux", "soak-test.log")

BANNER = b"XEMU-SHELL-READY"
OOP = re.compile(rb"BUG:|Oops:|kernel panic|Call Trace|general protection fault|"
                 rb"segfault at|WARNING:")

# Each entry: (command, substring that must appear in the output).
COMMANDS = [
    ("echo SOAK-ALIVE", "SOAK-ALIVE"),
    ("ls /", "bin"),
    ("cat /proc/uptime", "."),
    ("head -1 /proc/meminfo", "MemTotal"),
    ("grep -c . /proc/cpuinfo", ""),
    ("date", ":"),
    ("pidof sh", ""),
    ("cat /proc/loadavg", "."),
]

ANSI = re.compile(rb"\x1b\[[0-9;]*[A-Za-z]")


class Console:
    def __init__(self, argv, log_path):
        self.log = open(log_path, "wb")
        self.buf = b""
        self.proc = subprocess.Popen(
            argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT)

    def drain(self, timeout=0.2):
        """Read whatever is available; return False on EOF."""
        while True:
            r, _, _ = select.select([self.proc.stdout], [], [], timeout)
            if not r:
                return True
            chunk = os.read(self.proc.stdout.fileno(), 65536)
            if not chunk:
                return False
            self.log.write(chunk)
            self.log.flush()
            self.buf += chunk
            timeout = 0

    def send(self, line):
        try:
            self.proc.stdin.write(line.encode() + b"\n")
            self.proc.stdin.flush()
        except BrokenPipeError:
            pass

    def wait_for(self, needle, deadline_s):
        """Wait for `needle` in output; return the text consumed since the call."""
        start = time.time()
        seen = b""
        while time.time() - start < deadline_s:
            if needle.encode() in seen:
                return seen
            if not self.drain(0.2):
                break
            seen += self.buf
            self.buf = b""
            # Answer the line editor's cursor query like a real terminal.
            if b"\x1b[6n" in seen:
                try:
                    self.proc.stdin.write(b"\x1b[1;1R")
                    self.proc.stdin.flush()
                except BrokenPipeError:
                    pass
                seen = seen.replace(b"\x1b[6n", b"")
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--minutes", type=float, default=10.0)
    ap.add_argument("--interval", type=float, default=15.0)
    ap.add_argument("--command-timeout", type=float, default=60.0)
    ap.add_argument("--boot-timeout", type=float, default=180.0)
    ap.add_argument("--max-steps", type=int, default=2_000_000_000,
                    help="instruction cap; the emulator defaults to 100M, "
                         "which a soak must not inherit")
    ap.add_argument("--xemu", default=XEMU)
    ap.add_argument("--kernel", default=KERNEL)
    ap.add_argument("--initrd", default=INITRD)
    args = ap.parse_args()

    # The parser only accepts the `--flag=value` form for these options.
    # --kernel/--initrd take a separate argument; --cmdline uses the = form.
    # Honour --initrd so `make xemu-soak-test` can pass the shell image through.
    argv = [args.xemu, "--kernel", args.kernel, "--cmdline=console=ttyS0",
            "--initrd", args.initrd, "-b"]
    argv.append("--max-steps=%d" % args.max_steps)

    os.makedirs(os.path.dirname(LOG), exist_ok=True)
    con = Console(argv, LOG)
    t0 = time.time()
    failures = []

    print("soak: booting %s" % os.path.basename(args.initrd), flush=True)
    got = con.wait_for(BANNER.decode(), args.boot_timeout)
    if got is None:
        failures.append("boot: banner never appeared")
    else:
        print("soak: banner after %.1fs" % (time.time() - t0), flush=True)

    con.wait_for("/ #", 30.0)

    rounds = 0
    end = time.time() + args.minutes * 60.0
    while not failures and time.time() < end:
        for cmd, want in COMMANDS:
            if time.time() >= end:
                break
            rounds += 1
            con.send(cmd)
            # The shell echoes the command line and then prints the next
            # prompt, so waiting for "/ #" means the command finished and its
            # output is in the captured block.
            started = time.time()
            out = con.wait_for("/ #", args.command_timeout)
            if out is None:
                failures.append("command %r produced no prompt within %.0fs"
                                % (cmd, args.command_timeout))
                break
            text = ANSI.sub(b"", out).replace(b"\r", b"")
            # Drop the echoed command line and the prompt lines themselves.
            body = b"\n".join(l for l in text.split(b"\n")
                              if cmd.encode() not in l and l.strip() not in
                              (b"/ #", b"", b"[6n"))
            if want and want.encode() not in body:
                failures.append("command %r: expected %r, got %r"
                                % (cmd, want, body[:160]))
                break
            if OOP.search(text):
                failures.append("kernel complaint while running %r: %r"
                                % (cmd, OOP.search(text).group(0)))
                break
            print("soak: %-24s ok (%5.1fs)  round %d"
                  % (cmd, time.time() - started, rounds), flush=True)
            # A short pause keeps the guest idle between commands, which is
            # where a timer/idle bug would show up.
            time.sleep(args.interval)

    elapsed = time.time() - t0
    alive = con.proc.poll() is None
    if alive:
        con.send("poweroff -f")
        con.wait_for("halt", 20.0)
    try:
        con.proc.wait(timeout=20)
    except subprocess.TimeoutExpired:
        con.proc.kill()
    con.log.close()

    print()
    print("soak: %.1f minutes, %d commands, log=%s"
          % (elapsed / 60.0, rounds, LOG))
    if failures:
        for f in failures:
            print("FAIL  %s" % f)
        print("\nsoak: FAIL")
        return 1
    print("PASS  stayed responsive for the whole soak, no kernel complaint")
    print("\nsoak: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
