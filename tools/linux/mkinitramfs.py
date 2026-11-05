#!/usr/bin/env python3
"""mkinitramfs.py -- build the XEMU Linux test initramfs (cpio "newc").

The image contains a statically linked, libc-free ``/init`` that

  1. writes ``XEMU: /init running as pid 1`` to fd 1 and fd 2,
  2. writes ``XEMU: initramfs mounted, shutting down``,
  3. asks the kernel to power off (``reboot(RB_POWER_OFF)``),
     falling back to ``RB_HALT_SYSTEM`` and finally ``exit_group(0)``.

``/init`` is compiled with ``x86_64-linux-gnu-gcc -static -nostdlib`` so it
contains no libc at all: only the raw ``write``/``reboot``/``exit_group``
syscalls.  That keeps the kernel's job at exec time as small as possible
(no dynamic loader, no TLS setup beyond what the kernel does for any ELF).

Outputs (both under build/linux/ by default):

    initramfs.cpio       uncompressed newc archive, for inspection
    initramfs.cpio.gz    gzip -9 compressed image handed to the kernel

Usage::

    tools/linux/mkinitramfs.py [--out FILE] [--busybox PATH] [--cc PATH]

If no x86_64 cross compiler is on PATH (e.g. on an Apple Silicon macOS
host) the script re-runs itself inside the persistent ``xemu-kbuild``
Debian arm64 container used by ``build_kernel.sh``.
"""

import argparse
import gzip
import io
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
BUILD_DIR = REPO_ROOT / "build" / "linux"
DEFAULT_OUT = BUILD_DIR / "initramfs.cpio.gz"
CONTAINER = os.environ.get("KBUILD_CONTAINER", "xemu-kbuild")
CROSS_CC = os.environ.get("CROSS_COMPILE", "x86_64-linux-gnu-") + "gcc"
APT_PACKAGES = (
    "gcc-x86-64-linux-gnu gcc libc6-dev make bison flex bc libssl-dev libelf-dev "
    "cpio xz-utils gzip ca-certificates wget file binutils-x86-64-linux-gnu python3"
)

# ---------------------------------------------------------------------------
# /init, freestanding C (no libc, no startup files).
# ---------------------------------------------------------------------------
INIT_C = r"""
/* XEMU test init -- pid 1 inside the initramfs.
 *
 * Deliberately libc-free: the kernel only has to map the ELF and jump to
 * _start.  Only three syscalls are used: write(2), reboot(2), exit_group(2).
 */

typedef unsigned long u64;

#define SYS_write       1
#define SYS_reboot      169
#define SYS_exit_group  231

#define LINUX_REBOOT_MAGIC1  0xfee1dead
#define LINUX_REBOOT_MAGIC2  672274793        /* 0x28121969 */
#define RB_POWER_OFF         0x4321fedc
#define RB_HALT_SYSTEM       0xcdef0123

static inline long syscall1(long n, long a1)
{
	long r;
	__asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a1)
			 : "rcx", "r11", "memory");
	return r;
}

static inline long syscall3(long n, long a1, long a2, long a3)
{
	long r;
	__asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a1), "S"(a2), "d"(a3)
			 : "rcx", "r11", "memory");
	return r;
}

static inline long syscall4(long n, long a1, long a2, long a3, long a4)
{
	register long r10 __asm__("r10") = a4;
	long r;
	__asm__ volatile("syscall" : "=a"(r)
			 : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
			 : "rcx", "r11", "memory");
	return r;
}

static void write_fd(int fd, const char *msg, u64 len)
{
	u64 off = 0;

	while (off < len) {
		long rc = syscall3(SYS_write, fd, (long)(msg + off),
				   (long)(len - off));
		if (rc == -4)          /* -EINTR */
			continue;
		if (rc <= 0)           /* hard error: nothing else we can do */
			break;
		off += (u64)rc;
	}
}

static void write_both(const char *msg, u64 len)
{
	write_fd(1, msg, len);
	write_fd(2, msg, len);
}

void _start(void)
{
	static const char msg_run[]   = "XEMU: /init running as pid 1\n";
	static const char msg_down[]  = "XEMU: initramfs mounted, shutting down\n";

	write_both(msg_run, sizeof(msg_run) - 1);

	/*
	 * devtmpfs is mounted by the kernel itself (CONFIG_DEVTMPFS_MOUNT=y)
	 * before init runs, so "initramfs mounted" is already true here.
	 */
	write_both(msg_down, sizeof(msg_down) - 1);

	/* Prefer a clean power-off; the emulator may turn this into a halt. */
	syscall4(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
		 RB_POWER_OFF, 0);

	/* No ACPI/PM in the XEMU kernel, so power-off usually cannot happen. */
	syscall4(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
		 RB_HALT_SYSTEM, 0);

	/* Last resort: exiting as pid 1 panics the kernel ("Attempted to kill
	 * init"), which is a clean, visible end state for a boot test. */
	syscall1(SYS_exit_group, 0);

	/* exit_group never returns; if it somehow did, spin on a nop (HLT is
	 * privileged and would #GP in ring 3). */
	for (;;)
		__asm__ volatile("nop");
}
"""

# ---------------------------------------------------------------------------
# newc cpio writer
# ---------------------------------------------------------------------------
CPIO_MAGIC = b"070701"


def cpio_header(name, ino, mode, nlink, filesize, rdevmajor=0, rdevminor=0,
                uid=0, gid=0, mtime=0):
    fields = [
        ino, mode, uid, gid, nlink, mtime, filesize,
        0, 0,                          # devmajor, devminor
        rdevmajor, rdevminor,
        len(name) + 1,                 # namesize (name + NUL)
        0,                             # check
    ]
    hdr = CPIO_MAGIC + b"".join(b"%08X" % (f & 0xFFFFFFFF) for f in fields)
    assert len(hdr) == 110, len(hdr)
    return hdr


def cpio_entry(name, ino, mode, data=b"", nlink=1, rdevmajor=0, rdevminor=0):
    name_b = name.encode() + b"\0"
    out = bytearray()
    out += cpio_header(name, ino, mode, nlink, len(data), rdevmajor, rdevminor)
    out += name_b
    out += b"\0" * ((-len(out)) % 4)
    out += data
    out += b"\0" * ((-len(data)) % 4)
    return bytes(out)


def build_cpio(entries):
    """entries: list of (name, mode, data, nlink, rdevmajor, rdevminor)."""
    out = bytearray()
    for ino, (name, mode, data, nlink, rdevmajor, rdevminor) in enumerate(
            entries, start=1):
        out += cpio_entry(name, ino, mode, data, nlink, rdevmajor, rdevminor)
    out += cpio_entry("TRAILER!!!", 0, 0, b"", nlink=1)
    out += b"\0" * ((-len(out)) % 512)
    return bytes(out)


def default_entries(init_bin, busybox=None):
    entries = [
        # Directories first: the initramfs unpacker needs the parents.
        ("dev", 0o040755, b"", 2, 0, 0),
        ("proc", 0o040755, b"", 2, 0, 0),
        ("sys", 0o040755, b"", 2, 0, 0),
        ("tmp", 0o041777, b"", 2, 0, 0),
        # Device nodes; devtmpfs is mounted over /dev by the kernel, these
        # only exist so that the mount point is guaranteed to be present.
        ("dev/console", 0o020600, b"", 1, 5, 1),
        ("dev/null", 0o020666, b"", 1, 1, 3),
        ("dev/tty", 0o020666, b"", 1, 5, 0),
        ("init", 0o100755, init_bin, 1, 0, 0),
    ]
    if busybox is not None:
        bb = Path(busybox).read_bytes()
        entries.append(("bin", 0o040755, b"", 2, 0, 0))
        entries.append(("bin/busybox", 0o100755, bb, 1, 0, 0))
        for applet in ("sh", "ls", "cat", "echo", "mount", "poweroff"):
            entries.append(("bin/" + applet, 0o120777,
                            ("busybox").encode(), 1, 0, 0))
    return entries


# ---------------------------------------------------------------------------
# toolchain handling
# ---------------------------------------------------------------------------
def gcc_in_container():
    return subprocess.run(
        ["docker", "exec", CONTAINER, "sh", "-c", "command -v " + CROSS_CC],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0


def ensure_docker_container():
    if shutil.which("docker") is None:
        sys.exit("error: %s not found and docker is not available" % CROSS_CC)
    check = subprocess.run(["docker", "inspect", CONTAINER],
                           stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
    if check.returncode != 0:
        print("==> creating container %s (debian:bookworm, arm64)" % CONTAINER,
              file=sys.stderr)
        subprocess.run(["docker", "run", "-d", "--name", CONTAINER,
                        "-v", "%s:/work" % REPO_ROOT, "-w", "/work",
                        "debian:bookworm", "sleep", "infinity"], check=True,
                       stdout=subprocess.DEVNULL)
    else:
        subprocess.run(["docker", "start", CONTAINER],
                       stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
    if not gcc_in_container():
        print("==> installing cross toolchain in %s" % CONTAINER,
              file=sys.stderr)
        subprocess.run(
            ["docker", "exec", CONTAINER, "sh", "-c",
             "apt-get update -qq && DEBIAN_FRONTEND=noninteractive "
             "apt-get install -y -qq --no-install-recommends " + APT_PACKAGES],
            check=True)


def maybe_reexec_in_container(argv):
    """Return a process exit code if we handed the job to docker, else None."""
    if shutil.which(CROSS_CC) is not None:
        return None
    if os.environ.get("KBUILD_IN_CONTAINER") == "1":
        sys.exit("error: %s missing inside the build container" % CROSS_CC)
    ensure_docker_container()
    print("==> no local %s, re-running inside container %s"
          % (CROSS_CC, CONTAINER), file=sys.stderr)
    cmd = ["docker", "exec", "-e", "KBUILD_IN_CONTAINER=1", CONTAINER,
           "python3", "/work/tools/linux/mkinitramfs.py"] + argv
    return subprocess.run(cmd).returncode


def compile_init(cc, workdir):
    src = workdir / "init.c"
    out = workdir / "init"
    src.write_text(INIT_C)
    cmd = [
        cc,
        "-static", "-nostdlib", "-ffreestanding", "-fno-stack-protector",
        "-fno-pie", "-no-pie", "-Os", "-mno-red-zone",
        "-Wl,-e,_start", "-Wl,--build-id=none",
        "-o", str(out), str(src),
    ]
    print("==> compiling /init: " + " ".join(cmd), file=sys.stderr)
    subprocess.run(cmd, check=True)
    return out


def main(argv):
    ap = argparse.ArgumentParser(description="build the XEMU initramfs")
    ap.add_argument("--out", default=str(DEFAULT_OUT),
                    help="gzip image output path (default %(default)s)")
    ap.add_argument("--cc", default=os.environ.get("CC", CROSS_CC),
                    help="x86_64 cross compiler (default %(default)s)")
    ap.add_argument("--busybox", default=None,
                    help="optional static x86_64 busybox to include as "
                         "/bin/busybox (not used by /init)")
    args = ap.parse_args(argv)

    rc = maybe_reexec_in_container(argv)
    if rc is not None:
        return rc

    out_gz = Path(args.out).resolve()
    workdir = BUILD_DIR / "initramfs"
    workdir.mkdir(parents=True, exist_ok=True)

    init_bin = compile_init(args.cc, workdir)
    busybox = args.busybox
    if busybox is not None and not Path(busybox).is_file():
        sys.exit("error: busybox not found: %s" % busybox)

    entries = default_entries(init_bin.read_bytes(), busybox)
    archive = build_cpio(entries)

    out_cpio = out_gz.with_suffix("") if out_gz.suffix == ".gz" else \
        out_gz.with_name(out_gz.name + ".cpio")
    out_cpio.write_bytes(archive)

    buf = io.BytesIO()
    with gzip.GzipFile(filename="", mode="wb", fileobj=buf,
                       compresslevel=9, mtime=0) as gz:
        gz.write(archive)
    out_gz.write_bytes(buf.getvalue())

    print("initramfs entries: " + ", ".join("/" + n for n, *_ in entries))
    print("wrote %s (%d bytes)" % (out_cpio, len(archive)))
    print("wrote %s (%d bytes)" % (out_gz, len(buf.getvalue())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
