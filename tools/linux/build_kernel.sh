#!/usr/bin/env bash
#
# build_kernel.sh -- build the minimal x86_64 Linux kernel used by XEMU.
#
# What it does
#   1. downloads (and sha256-verifies) a pinned Linux 6.6.x stable tarball
#      into build/linux/dl/,
#   2. extracts it to build/linux/linux-$VERSION/ (build/ is gitignored),
#   3. starts from `make ARCH=x86_64 tinyconfig` and enables exactly the
#      options needed to reach /init on a serial console (see the arrays
#      below; every knob is recorded in build/linux/build-info.txt),
#   4. cross-builds the unstripped vmlinux and copies it to build/linux/,
#   5. builds build/linux/initramfs.cpio.gz via tools/linux/mkinitramfs.py,
#   6. verifies the artifacts (ELF type, PT_LOAD p_paddr, symbols, cpio
#      magic, gzip integrity) and writes build/linux/build-info.txt.
#
# Requirements
#   Linux host with x86_64-linux-gnu-gcc (Debian package
#   gcc-x86-64-linux-gnu on arm64), or Docker.  On macOS the script
#   automatically re-runs itself inside the persistent arm64 container
#   "xemu-kbuild" (created on first use, Debian bookworm) so everything
#   builds natively on Apple Silicon -- no --platform emulation.
#
# Usage
#   tools/linux/build_kernel.sh            # kernel + initramfs + verification
#   tools/linux/build_kernel.sh --config   # (re)configure only
#   JOBS=4 tools/linux/build_kernel.sh     # limit parallelism
#
set -euo pipefail

LINUX_VERSION="${LINUX_VERSION:-6.6.60}"
LINUX_URL="${LINUX_URL:-https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${LINUX_VERSION}.tar.xz}"
LINUX_SHA256="${LINUX_SHA256:-52f9e32d5082ab94253447fd66670d0c3bb765cfcb99b0bf61d1b8eae25952ef}"
# sha256 above is from https://cdn.kernel.org/pub/linux/kernel/v6.x/sha256sums.asc

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
BUILD_DIR="$REPO_ROOT/build/linux"
DL_DIR="$BUILD_DIR/dl"
SRC_DIR="$BUILD_DIR/linux-$LINUX_VERSION"
TARBALL="$DL_DIR/linux-$LINUX_VERSION.tar.xz"
CROSS="${CROSS_COMPILE:-x86_64-linux-gnu-}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
CONTAINER="${KBUILD_CONTAINER:-xemu-kbuild}"
APT_PACKAGES="gcc-x86-64-linux-gnu gcc libc6-dev make bison flex bc libssl-dev libelf-dev
cpio xz-utils gzip ca-certificates wget curl file binutils-x86-64-linux-gnu python3"
DO_CONFIG_ONLY=0

if [[ "${1:-}" == "--config" ]]; then DO_CONFIG_ONLY=1; fi
if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
	sed -n '2,30p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
	exit 0
fi

log() { printf '==> %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# Re-execute inside the arm64 Debian container when no native x86_64 cross
# compiler is available (macOS host).
# ---------------------------------------------------------------------------
have_cross_gcc() { command -v "${CROSS}gcc" >/dev/null 2>&1; }

if ! have_cross_gcc; then
	if [[ "${KBUILD_IN_CONTAINER:-0}" == "1" ]]; then
		die "${CROSS}gcc missing inside the build container"
	fi
	command -v docker >/dev/null 2>&1 || \
		die "no ${CROSS}gcc and no docker; install gcc-x86-64-linux-gnu"

	if ! docker inspect "$CONTAINER" >/dev/null 2>&1; then
		log "creating container $CONTAINER (debian:bookworm, native $(uname -m))"
		docker run -d --name "$CONTAINER" -v "$REPO_ROOT:/work" -w /work \
			debian:bookworm sleep infinity >/dev/null
	else
		docker start "$CONTAINER" >/dev/null 2>&1 || true
	fi
	if ! docker exec "$CONTAINER" sh -c "command -v ${CROSS}gcc" >/dev/null 2>&1; then
		log "installing cross toolchain in $CONTAINER"
		# shellcheck disable=SC2086
		docker exec "$CONTAINER" sh -c \
			"apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends $APT_PACKAGES"
	fi
	log "re-running build inside container $CONTAINER"
	exec docker exec -e KBUILD_IN_CONTAINER=1 -e JOBS="$JOBS" \
		-e LINUX_VERSION="$LINUX_VERSION" -e LINUX_URL="$LINUX_URL" \
		-e LINUX_SHA256="$LINUX_SHA256" -w /work \
		"$CONTAINER" /work/tools/linux/build_kernel.sh "$@"
fi

# ---------------------------------------------------------------------------
# Config knobs.  Everything here is recorded verbatim in build-info.txt.
# The intent: smallest kernel that still prints to ttyS0 and execs /init
# from the initramfs, with nothing that needs CPU features beyond the
# x86-64 / SSE2 baseline (and no SSE at all: the kernel builds -mno-sse).
# ---------------------------------------------------------------------------
ENABLE=(
	# --- boot protocol / core -------------------------------------------------
	64BIT
	EXPERT                 # unhides X86_UMIP / PROCESSOR_SELECT / MULTIUSER knobs
	MCORE2                 # -march=core2: x86-64 baseline, no AVX; kernel adds -mno-sse
	# --- console --------------------------------------------------------------
	PRINTK
	TTY
	SERIAL_8250
	SERIAL_8250_CONSOLE
	SERIAL_CORE
	SERIAL_CORE_CONSOLE
	SERIAL_EARLYCON
	EARLY_PRINTK           # for earlyprintk=serial,ttyS0,115200
	# --- initramfs + userland -------------------------------------------------
	BLK_DEV_INITRD
	RD_GZIP
	BINFMT_ELF
	BINFMT_SCRIPT
	MULTIUSER
	DEVTMPFS
	DEVTMPFS_MOUNT
	PROC_FS
	SYSFS
	TMPFS
	SHMEM
	# --- debug aids -----------------------------------------------------------
	KALLSYMS               # runtime symbol names in oops/panic output
	KALLSYMS_ALL           # include data symbols too (cheap, helps GDB/kallsyms)
	BUG                    # keep BUG()/WARN() diagnostics
	X86_VERBOSE_BOOTUP
	FRAME_POINTER
	UNWINDER_FRAME_POINTER # selects FRAME_POINTER, no ORC/objtool pass needed
	# --- tuning for an interpreter -------------------------------------------
	HZ_100                 # 100 timer interrupts/s instead of 250
	NO_HZ_IDLE             # stop the tick while the CPU is idle
	PREEMPT_NONE
	IO_DELAY_NONE          # do not poke port 0x80 on every in/out_p
	CC_OPTIMIZE_FOR_PERFORMANCE  # -O2 (fewer executed instructions than -Os)
	DEBUG_INFO_NONE        # no DWARF; .symtab + kallsyms are kept
	LEGACY_VSYSCALL_NONE   # no fixed vsyscall page to map/emulate
	INIT_STACK_NONE        # do NOT zero every stack variable (-ftrivial-auto-var-init)
	CPU_SUP_INTEL
	PROCESSOR_SELECT
)

DISABLE=(
	# --- CPU / platform -------------------------------------------------------
	SMP
	X86_5LEVEL
	GENERIC_CPU            # processor-family choice: only MCORE2 stays =y
	MK8
	MPSC
	MATOM
	HZ_250                 # timer choice: only HZ_100 stays =y
	HZ_300
	HZ_1000
	CC_OPTIMIZE_FOR_SIZE   # -O2, not -Os
	RANDOMIZE_BASE
	RELOCATABLE            # fixed physical load address 0x1000000
	RANDOMIZE_MEMORY
	X86_UMIP
	X86_SMAP               # not present in 6.6 (runtime CPUID-gated)
	X86_SMEP               # not present in 6.6 (runtime CPUID-gated)
	X86_INTEL_MEMORY_PROTECTION_KEYS
	X86_USER_SHADOW_STACK
	X86_KERNEL_IBT
	CPU_SUP_AMD
	CPU_SUP_HYGON
	CPU_SUP_CENTAUR
	CPU_SUP_ZHAOXIN
	CPU_SUP_VORTEX_32
	ACPI
	PCI
	EFI
	EFI_STUB
	HYPERVISOR_GUEST
	X86_MCE
	MODULES
	IA32_EMULATION
	X86_X32
	PNP
	ISAPNP
	# --- mitigations / hardening (all cost instructions) ----------------------
	CPU_MITIGATIONS
	MITIGATION_RFDS
	MITIGATION_SPECTRE_BHI
	RETPOLINE
	RETHUNK
	CPU_UNRET_ENTRY
	CALL_DEPTH_TRACKING
	SLS
	PAGE_TABLE_ISOLATION
	STACKPROTECTOR
	STACKPROTECTOR_STRONG
	STACKPROTECTOR_PER_TASK
	# --- tracing / instrumentation -------------------------------------------
	FUNCTION_TRACER
	FTRACE
	DYNAMIC_FTRACE
	FUNCTION_GRAPH_TRACER
	STACK_TRACER
	IRQSOFF_TRACER
	PREEMPT_TRACER
	SCHED_TRACER
	HWLAT_TRACER
	OSNOISE_TRACER
	KPROBES
	KPROBE_EVENTS
	UPROBES
	UPROBE_EVENTS
	FPROBE
	BPF_SYSCALL
	BPF_JIT
	PERF_EVENTS
	PROFILING
	KASAN
	KCSAN
	KMSAN
	KFENCE
	UBSAN
	KCOV
	GCOV_KERNEL
	STACK_VALIDATION
	UNWINDER_ORC
	UNWINDER_GUESS
	# --- debug info -----------------------------------------------------------
	DEBUG_INFO
	DEBUG_INFO_BTF
	DEBUG_INFO_DWARF4
	DEBUG_INFO_DWARF5
	DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT
	DEBUG_INFO_REDUCED
	DEBUG_INFO_SPLIT
	GDB_SCRIPTS
	DEBUG_FS
	MAGIC_SYSRQ
	LOCKUP_DETECTOR
	SOFTLOCKUP_DETECTOR
	HARDLOCKUP_DETECTOR
	DETECT_HUNG_TASK
	SCHED_DEBUG
	DEBUG_PREEMPT
	PROVE_LOCKING
	DEBUG_ATOMIC_SLEEP
	DEBUG_LIST
	DEBUG_OBJECTS
	INIT_STACK_ALL_ZERO
	INIT_STACK_ALL_PATTERN
	DEBUG_MUTEXES
	DEBUG_SPINLOCK
	DEBUG_VM
	DEBUG_KMEMLEAK
	# --- devices / buses (nothing to drive) ----------------------------------
	VT
	VT_CONSOLE
	HW_CONSOLE
	DUMMY_CONSOLE
	VGA_CONSOLE
	INPUT
	INPUT_KEYBOARD
	HID
	HID_SUPPORT
	USB
	USB_SUPPORT
	MMC
	SCSI
	BLOCK
	NET
	SOUND
	DRM
	FB
	RTC_CLASS
	RTC_HCTOSYS
	SERIO                  # keyboard/serial input layer (INPUT is off anyway)
	SERIO_I8042
	SERIO_SERPORT
	SERIO_LIBPS2
	SERIO_RAW
	LEGACY_PTYS
	LEGACY_TIOCSTI
	LDISC_AUTOLOAD
	RD_BZIP2               # only gzip initramfs is used
	RD_LZMA
	RD_XZ
	RD_LZO
	RD_LZ4
	RD_ZSTD
	HPET
	X86_PM_TIMER
	SERIAL_8250_PNP
	SERIAL_8250_PCI
	SERIAL_8250_DW
	SERIAL_8250_EXAR
	SERIAL_8250_EXTENDED
	SERIAL_8250_RSA
	SERIAL_8250_DEPRECATED_OPTIONS
	# --- subsystems that only add boot work ----------------------------------
	PM
	PM_SLEEP
	SUSPEND
	HIBERNATION
	CPU_IDLE
	CPU_FREQ
	THERMAL
	FW_LOADER
	SECCOMP
	AUDIT
	CGROUPS
	NAMESPACES
	PSI
	IO_URING
	RSEQ
	POSIX_TIMERS
	SYSVIPC
	KEXEC
	KEXEC_FILE
	CRASH_DUMP
	PROC_KCORE
	SWAP
	DMIID
	ARCH_RANDOM
	RANDOMIZE_KSTACK_OFFSET
	GCC_PLUGINS
)

SETVAL=(
	"NR_CPUS 1"
	"SERIAL_8250_NR_UARTS 1"
	"SERIAL_8250_RUNTIME_UARTS 1"
	"PHYSICAL_START 0x1000000"
	"LOG_BUF_SHIFT 16"
	"DEFAULT_CONSOLE_LOGLEVEL 7"
	"CONSOLE_LOGLEVEL_DEFAULT 7"
)

KEY_SYMBOLS=(
	64BIT SMP NR_CPUS X86_5LEVEL RANDOMIZE_BASE RELOCATABLE
	MCORE2 GENERIC_CPU MATOM MK8 MPSC
	PRINTK TTY SERIAL_8250 SERIAL_8250_CONSOLE SERIAL_8250_PNP
	SERIAL_CORE_CONSOLE SERIAL_EARLYCON EARLY_PRINTK
	BLK_DEV_INITRD RD_GZIP BINFMT_ELF BINFMT_SCRIPT MULTIUSER
	DEVTMPFS DEVTMPFS_MOUNT PROC_FS SYSFS TMPFS SHMEM
	ACPI PCI EFI HYPERVISOR_GUEST X86_MCE MODULES IA32_EMULATION
	KALLSYMS KALLSYMS_ALL KALLSYMS_BASE_RELATIVE BUG FRAME_POINTER
	UNWINDER_FRAME_POINTER DEBUG_INFO DEBUG_INFO_NONE
	X86_UMIP X86_INTEL_MEMORY_PROTECTION_KEYS CPU_SUP_INTEL CPU_SUP_AMD
	CPU_MITIGATIONS RETPOLINE RETHUNK PAGE_TABLE_ISOLATION
	STACKPROTECTOR HZ NO_HZ_IDLE PREEMPT_NONE IO_DELAY_NONE
	HPET_TIMER RTC_CLASS CPU_IDLE PM BLOCK NET EXPERT
	LEGACY_VSYSCALL_NONE CC_OPTIMIZE_FOR_PERFORMANCE
)

# ---------------------------------------------------------------------------
# Source staging
# ---------------------------------------------------------------------------
mkdir -p "$DL_DIR" "$BUILD_DIR"

check_sum() {
	if command -v sha256sum >/dev/null 2>&1; then
		echo "$LINUX_SHA256  $1" | sha256sum -c -
	else
		echo "$LINUX_SHA256  $1" | shasum -a 256 -c -
	fi
}

if [[ ! -f "$TARBALL" ]]; then
	log "downloading $LINUX_URL"
	if command -v curl >/dev/null 2>&1; then
		curl -fL --retry 3 -o "$TARBALL.part" "$LINUX_URL"
	elif command -v wget >/dev/null 2>&1; then
		wget -O "$TARBALL.part" "$LINUX_URL"
	else
		die "need curl or wget to download the kernel source"
	fi
	mv "$TARBALL.part" "$TARBALL"
fi
log "verifying sha256 of $(basename "$TARBALL")"
check_sum "$TARBALL" >/dev/null || die "sha256 mismatch for $TARBALL"

if [[ ! -f "$SRC_DIR/Makefile" ]]; then
	log "extracting $(basename "$TARBALL")"
	tar -xJf "$TARBALL" -C "$BUILD_DIR"
fi
[[ -f "$SRC_DIR/Makefile" ]] || die "kernel source tree missing at $SRC_DIR"

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
SCFG="$SRC_DIR/scripts/config"
CONF="$SRC_DIR/.config"

sym_known() {
	grep -qE "^(CONFIG_$1=|# CONFIG_$1 is not set)" "$CONF"
}

SKIPPED=()
apply_knobs() {
	local sym
	SKIPPED=()
	for sym in "${ENABLE[@]}"; do
		if sym_known "$sym"; then "$SCFG" --file "$CONF" -e "$sym"; else
			case " ${SKIPPED[*]-} " in *" $sym "*) ;; *) SKIPPED+=("$sym") ;; esac
		fi
	done
	for sym in "${DISABLE[@]}"; do
		if sym_known "$sym"; then "$SCFG" --file "$CONF" -d "$sym"; else
			case " ${SKIPPED[*]-} " in *" $sym "*) ;; *) SKIPPED+=("$sym") ;; esac
		fi
	done
	local kv
	for kv in "${SETVAL[@]}"; do
		set -- $kv
		if sym_known "$1"; then "$SCFG" --file "$CONF" --set-val "$1" "$2"; else
			case " ${SKIPPED[*]-} " in *" $1 "*) ;; *) SKIPPED+=("$1") ;; esac
		fi
	done
}

config_get() {
	local v
	v=$(sed -n "s/^CONFIG_$1=//p" "$CONF" | head -n 1)
	# A symbol that is absent from .config is either "not set" or hidden
	# behind unmet dependencies; in both cases its effective value is n.
	[[ -n "$v" ]] || v=n
	printf '%s' "$v"
}

log "configuring linux-$LINUX_VERSION (ARCH=x86_64, tinyconfig base)"
make -C "$SRC_DIR" ARCH=x86_64 CROSS_COMPILE="$CROSS" tinyconfig >/dev/null

# Three rounds: knobs that depend on other knobs only become settable after
# the first olddefconfig has resolved the dependencies.
for round in 1 2 3; do
	apply_knobs
	make -C "$SRC_DIR" ARCH=x86_64 CROSS_COMPILE="$CROSS" olddefconfig >/dev/null
done

# ---------------------------------------------------------------------------
# Verify the options this kernel absolutely needs.
# ---------------------------------------------------------------------------
verify_bad=0
need_y() {
	[[ "$(config_get "$1")" == "y" ]] || { echo "  ERROR: CONFIG_$1 != y" >&2; verify_bad=1; }
}
need_n() {
	[[ "$(config_get "$1")" != "y" ]] || { echo "  ERROR: CONFIG_$1 is enabled" >&2; verify_bad=1; }
}
log "verifying critical config symbols"
for s in 64BIT PRINTK TTY SERIAL_8250 SERIAL_8250_CONSOLE SERIAL_CORE_CONSOLE \
	BLK_DEV_INITRD RD_GZIP BINFMT_ELF BINFMT_SCRIPT MULTIUSER DEVTMPFS \
	DEVTMPFS_MOUNT PROC_FS SYSFS TMPFS SHMEM KALLSYMS MCORE2 EARLY_PRINTK \
	FRAME_POINTER; do
	need_y "$s"
done
for s in SMP MODULES ACPI PCI EFI IA32_EMULATION RANDOMIZE_BASE RELOCATABLE \
	X86_5LEVEL CPU_SUP_AMD RETPOLINE RETHUNK PAGE_TABLE_ISOLATION \
	STACKPROTECTOR CPU_MITIGATIONS X86_UMIP X86_INTEL_MEMORY_PROTECTION_KEYS \
	BLOCK NET VT VT_CONSOLE HW_CONSOLE INPUT HID USB MMC SCSI SOUND DRM RTC_CLASS; do
	need_n "$s"
done
[[ "$(config_get DEBUG_INFO)" != "y" ]] || { echo "  ERROR: DEBUG_INFO enabled" >&2; verify_bad=1; }
[[ "$(config_get HZ)" == "100" ]] || { echo "  ERROR: CONFIG_HZ=$(config_get HZ) != 100" >&2; verify_bad=1; }
[[ "$verify_bad" == 0 ]] || die "config verification failed (see above)"

cp -f "$CONF" "$BUILD_DIR/kernel.config"

if [[ "$DO_CONFIG_ONLY" == "1" ]]; then
	log "config only: wrote $BUILD_DIR/kernel.config"
	exit 0
fi

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
log "building vmlinux with -j$JOBS"
make -C "$SRC_DIR" ARCH=x86_64 CROSS_COMPILE="$CROSS" -j"$JOBS" vmlinux

log "installing artifacts into $BUILD_DIR"
cp -f "$SRC_DIR/vmlinux" "$BUILD_DIR/vmlinux"

log "building initramfs"
python3 "$REPO_ROOT/tools/linux/mkinitramfs.py" \
	--cc "${CROSS}gcc" \
	--out "$BUILD_DIR/initramfs.cpio.gz"

# ---------------------------------------------------------------------------
# Verify artifacts
# ---------------------------------------------------------------------------
VMLINUX="$BUILD_DIR/vmlinux"
INITRD="$BUILD_DIR/initramfs.cpio.gz"
CPIO="$BUILD_DIR/initramfs.cpio"

log "verifying artifacts"
# NOTE: keep the tool output in variables -- with `set -o pipefail`, a
# `producer | grep -q` pipeline fails with SIGPIPE (141) on a successful match.
FILE_OUT=$(file "$VMLINUX")
case "$FILE_OUT" in
	*"ELF 64-bit LSB executable, x86-64"*) ;;
	*) die "vmlinux is not an x86-64 ELF executable: $FILE_OUT" ;;
esac
READELF_OUT=$("${CROSS}readelf" -l "$VMLINUX")
grep -q 'LOAD' <<<"$READELF_OUT" || die "vmlinux has no PT_LOAD segments"
NM_OUT=$("${CROSS}nm" "$VMLINUX")
for sym in startup_64 start_kernel; do
	grep -qE " [TtDd] $sym$" <<<"$NM_OUT" || die "symbol $sym missing from vmlinux"
done
[[ "$(head -c 6 "$CPIO")" == "070701" ]] || die "initramfs.cpio is not a newc archive"
gzip -t "$INITRD" || die "gzip -t failed on $INITRD"

# ---------------------------------------------------------------------------
# build-info.txt
# ---------------------------------------------------------------------------
{
	echo "# XEMU minimal Linux kernel build info"
	echo
	echo "kernel_version: $LINUX_VERSION"
	echo "source_url:     $LINUX_URL"
	echo "source_sha256:  $LINUX_SHA256"
	echo "build_date_utc: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
	echo "arch:           $(uname -m) $(uname -s) (cross: ${CROSS}gcc)"
	echo "host:           $(uname -sr)"
	echo "compiler:       $(${CROSS}gcc -dumpfullversion -dumpversion 2>/dev/null || ${CROSS}gcc --version | head -1)"
	echo "binutils:       $(${CROSS}ld --version | head -1)"
	echo "make_jobs:      $JOBS"
	echo "build_cmd:      make ARCH=x86_64 CROSS_COMPILE=$CROSS -j$JOBS vmlinux"
	echo
	echo "## artifacts"
	for f in "$VMLINUX" "$CPIO" "$INITRD"; do
		if command -v sha256sum >/dev/null 2>&1; then
			h=$(sha256sum "$f" | cut -d' ' -f1)
		else
			h=$(shasum -a 256 "$f" | cut -d' ' -f1)
		fi
		echo "$(basename "$f"): $(stat -c %s "$f" 2>/dev/null || stat -f %z "$f") bytes sha256=$h"
	done
	echo
	echo "## config base"
	echo "make ARCH=x86_64 tinyconfig, then the knobs below via scripts/config,"
	echo "then make olddefconfig (3 rounds). Full final config: build/linux/kernel.config"
	echo
	echo "## config knobs requested (=y)"
	for s in "${ENABLE[@]}"; do echo "CONFIG_$s"; done | sort
	echo
	echo "## config knobs requested (=n)"
	for s in "${DISABLE[@]}"; do echo "# CONFIG_$s is not set"; done | sort
	echo
	echo "## numeric values requested"
	for kv in "${SETVAL[@]}"; do echo "CONFIG_${kv%% *}=${kv#* }"; done | sort
	echo
	if [[ ${#SKIPPED[@]} -gt 0 ]]; then
		echo "## knobs scripts/config could not set"
		echo "# absent from .config when we tried: either hidden because their"
		echo "# dependencies are disabled (effective value n, which is what we"
		echo "# want) or not present in this kernel version (e.g. X86_SMAP/SMEP"
		echo "# were removed in 5.x; they are gated by CPUID at runtime)."
		echo "# final values:"
		for s in "${SKIPPED[@]}"; do printf 'CONFIG_%-40s %s\n' "$s" "$(config_get "$s")"; done | sort
		echo
	fi
	echo "## key symbols, final values"
	for s in "${KEY_SYMBOLS[@]}"; do
		printf 'CONFIG_%-40s %s\n' "$s" "$(config_get "$s")"
	done
	echo
	echo "## symbol addresses (nm vmlinux)"
	"${CROSS}nm" "$VMLINUX" | grep -E " (startup_64|start_kernel|_text|_stext|_etext|_sdata|_edata|__bss_start|_end)$"
	echo
	echo "## PT_LOAD segments (readelf -l, note p_paddr)"
	"${CROSS}readelf" -l "$VMLINUX" | sed -n '/Program Headers:/,/Section to Segment/p'
} >"$BUILD_DIR/build-info.txt"

log "wrote $BUILD_DIR/build-info.txt"
log "done:"
ls -l "$VMLINUX" "$INITRD"
