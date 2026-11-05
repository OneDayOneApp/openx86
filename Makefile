SHELL := /bin/bash

SBT_HOME := $(CURDIR)/.home
SBT_ENV := SBT_OPTS="-Duser.home=$(SBT_HOME) -Dsbt.boot.directory=$(SBT_HOME)/.sbt/boot -Dsbt.global.base=$(SBT_HOME)/.sbt -Dsbt.ivy.home=$(SBT_HOME)/.ivy2 -Dsbt.coursier.home=$(SBT_HOME)/.coursier"
SBT := $(SBT_ENV) sbt -batch -Dsbt.color=false

RTL_DIR := build/rtl
DEC_DIR := build/decode
DEC_OBJ := build/obj_decode

.PHONY: all compile rtl decoder decode-vectors decode-sim decode-test clean

all: decode-test

compile:
	$(SBT) compile

rtl:
	$(SBT) "runMain x86.elaborate.EmitVerilog $(RTL_DIR)"

decoder:
	$(SBT) "runMain x86.elaborate.EmitDecoder $(RTL_DIR)"

decode-vectors:
	python3 tools/gen_decode_tests.py $(DEC_DIR)

SDK := $(shell xcrun --show-sdk-path)
HOST_CXX ?= /usr/bin/clang++
HOST_CXXFLAGS := -isysroot $(SDK) -nostdinc++ -isystem $(SDK)/usr/include/c++/v1 -std=c++17 -O2

decode-sim: decoder
	verilator --cc --exe -j 4 -Wno-fatal --top-module Decoder \
	  -Mdir $(DEC_OBJ) $(RTL_DIR)/Decoder.v sim/decoder_tb.cpp -o decoder_tb
	$(MAKE) -C $(DEC_OBJ) -f VDecoder.mk CXX=$(HOST_CXX) LINK=$(HOST_CXX) \
	  CXXFLAGS="$(HOST_CXXFLAGS)" LDFLAGS="-isysroot $(SDK) -Wl,-U,__Z15vl_time_stamp64v,-U,__Z13sc_time_stampv" -j4

decode-test: decode-sim decode-vectors
	$(DEC_OBJ)/decoder_tb $(DEC_DIR)/vectors.txt $(DEC_DIR)/image.bin

CORE_OBJ := build/obj_core

core-sim: rtl
	verilator --cc --exe -j 4 -Wno-fatal --top-module X86Top \
	  -Mdir $(CORE_OBJ) $(RTL_DIR)/X86Top.v sim/core_tb.cpp -o core_tb
	$(MAKE) -C $(CORE_OBJ) -f VX86Top.mk CXX=$(HOST_CXX) LINK=$(HOST_CXX) \
	  CXXFLAGS="$(HOST_CXXFLAGS)" LDFLAGS="-isysroot $(SDK) -Wl,-U,__Z15vl_time_stamp64v,-U,__Z13sc_time_stampv" -j4

TEST ?= tests/asm/t1_basic.S

test: core-sim
	python3 tools/difftest.py $(TEST)

regress: core-sim
	python3 tools/regress.py

OOO_OBJ := build/obj_ooo

ooo-rtl:
	$(SBT) "runMain x86.elaborate.EmitOoo $(RTL_DIR)"

ooo-sim: ooo-rtl
	verilator --cc --exe -j 4 -Wno-fatal --top-module X86OooTop \
	  -Mdir $(OOO_OBJ) $(RTL_DIR)/X86OooTop.v sim/ooo_tb.cpp -o ooo_tb
	$(MAKE) -C $(OOO_OBJ) -f VX86OooTop.mk CXX=$(HOST_CXX) LINK=$(HOST_CXX) \
	  CXXFLAGS="$(HOST_CXXFLAGS)" LDFLAGS="-isysroot $(SDK) -Wl,-U,__Z15vl_time_stamp64v,-U,__Z13sc_time_stampv" -j4

ooo-test: ooo-sim core-sim
	python3 tools/ooo_regress.py

sys-lockstep-test: core-sim xemu-so
	python3 tools/sys_lockstep.py

xemu-sys-test: xemu-bin
	python3 tools/xemu_sys.py

xemu-all-test: xemu-so xemu-bin isa-diff xemu-sys-test sys-lockstep-test emu-test

# --- Bochs golden reference (independent interpreter) ---------------------
BOCHS_VER := 2.8
BOCHS_DIR := .deps/bochs-$(BOCHS_VER)
BOCHS_BIN := $(BOCHS_DIR)/bochs

$(BOCHS_BIN):
	@mkdir -p .deps
	cd .deps && curl -sL -o bochs-$(BOCHS_VER).tar.gz \
	  https://downloads.sourceforge.net/project/bochs/bochs/$(BOCHS_VER)/bochs-$(BOCHS_VER).tar.gz
	cd .deps && tar xzf bochs-$(BOCHS_VER).tar.gz
	cd $(BOCHS_DIR) && ./configure --enable-debugger --with-sdl=no --with-x=no \
	  --with-win32=no --with-rfb=no --with-vncsrv=no --with-nogui \
	  --enable-cpu-level=6 --enable-x86-64
	$(MAKE) -C $(BOCHS_DIR) -j4

bochs-build: $(BOCHS_BIN)

build/bochs/boot.bin: tools/bochs/boot.S
	@mkdir -p build/bochs
	/opt/homebrew/opt/llvm/bin/clang --target=i386-none-elf -c $< -o build/bochs/boot.o
	/opt/homebrew/opt/llvm/bin/llvm-objcopy -O binary build/bochs/boot.o $@

build/bochs/floppy.img: build/bochs/boot.bin
	python3 -c "import pathlib; b=pathlib.Path('build/bochs/boot.bin').read_bytes(); \
	  pathlib.Path('build/bochs/floppy.img').write_bytes(bytes(b[:512])+b'\x00'*(1474560-512))"

bochs-img: build/bochs/floppy.img

bochs-test: bochs-build bochs-img xemu-so
	python3 tools/bochs/difftest.py $$(ls build/tests/t*.bin)

sys-diff-test: core-sim xemu-so
	python3 tools/sysrun.py tests/system/t22_cr.S --diff
	python3 tools/sysrun.py tests/system/t23_paging.S --diff
	python3 tools/sysrun.py tests/system/t25_trap.S --diff
	python3 tools/sysrun.py tests/system/t26_pf_trap.S --diff
	python3 tools/sysrun.py tests/system/t27_iret.S --diff
	python3 tools/sysrun.py tests/system/t28_syscall.S --diff
	python3 tools/sysrun.py tests/system/t29_cpuid.S --diff
	python3 tools/sysrun.py tests/system/t30_invlpg.S --diff
	python3 tools/sysrun.py tests/system/t31_fetchpf.S --diff
	python3 tools/sysrun.py tests/system/t32_intn.S --diff

sys-test: core-sim
	python3 tools/sysrun.py tests/system/t22_cr.S
	python3 tools/sysrun.py tests/system/t23_paging.S
	python3 tools/sysrun.py tests/system/t25_trap.S
	python3 tools/sysrun.py tests/system/t26_pf_trap.S
	python3 tools/sysrun.py tests/system/t27_iret.S
	python3 tools/sysrun.py tests/system/t28_syscall.S
	python3 tools/sysrun.py tests/system/t29_cpuid.S
	python3 tools/sysrun.py tests/system/t30_invlpg.S
	python3 tools/sysrun.py tests/system/t31_fetchpf.S
	python3 tools/sysrun.py tests/system/t32_intn.S

# ---------------------------------------------------------------- NEMU-style difftest / emu
# XEMU is the default reference model; the Unicorn-backed model stays available
# via `make ref-unicorn`.
REF_SO ?= build/ref/libxemu.so
UNICORN_REF := build/ref/libx86ref.so
UNICORN_DIR ?= $(CURDIR)/.home/pylibs/unicorn

.PHONY: ref-so ref-unicorn emu emu-test
ref-unicorn: $(UNICORN_REF)
ref-so: ref-unicorn

$(UNICORN_REF): ref/x86_ref.c ref/difftest_abi.h
	@mkdir -p build/ref
	/usr/bin/clang -isysroot $(SDK) -shared -fPIC -O2 -I $(UNICORN_DIR)/include \
	  ref/x86_ref.c -L $(UNICORN_DIR)/lib -lunicorn.2 \
	  -Wl,-rpath,$(UNICORN_DIR)/lib -o $@

# build/emu is the Verilator model + per-instruction difftest, mirroring
# XiangShan's `build/emu --diff --ref-so=...`.
emu: core-sim $(REF_SO)
	@printf '#!/bin/sh\nROOT=%s\nexec "$$ROOT/build/obj_core/core_tb" --diff --ref-so="$$ROOT/%s" "$$@"\n' "$(CURDIR)" "$(REF_SO)" > build/emu
	@chmod +x build/emu
	@echo "built build/emu (ref=$(REF_SO); usage: ./build/emu [--no-diff] <image.bin>)"

emu-test: emu
	python3 tools/emu_regress.py

emu-fuzz: emu
	python3 tools/emu_fuzz.py 200 1

# ---------------------------------------------------------------- XEMU (self-written reference model)
XEMU_SO := build/ref/libxemu.so
# The decode cache is the big win (~1.7x); -O3/LTO measured within noise of -O2
# on this interpreter, so keep the default at -O2 for fast builds.
XEMU_OPT ?= -O2
XEMU_MEM_MB ?= 16
XEMU_DEFS := -DXEMU_MEM_SIZE_MB=$(XEMU_MEM_MB)

XEMU_SRC := $(filter-out xemu/decode_test.c xemu/decode_field_test.c xemu/main.c xemu/isa_scan.c,$(wildcard xemu/*.c)) \
            $(wildcard xemu/dev/*.c)

.PHONY: xemu-so
xemu-so: $(XEMU_SO)

$(XEMU_SO): $(XEMU_SRC) xemu/xemu.h xemu/ops.h ref/difftest_abi.h
	@mkdir -p build/ref
	/usr/bin/clang -isysroot $(SDK) -std=c11 $(XEMU_OPT) -fPIC -shared $(XEMU_DEFS) \
	  -Wall -Wextra -o $@ $(XEMU_SRC) -lm

build/xemu: $(XEMU_SRC) xemu/main.c xemu/xemu.h xemu/ops.h
	@mkdir -p build
	/usr/bin/clang -isysroot $(SDK) -std=c11 $(XEMU_OPT) -Wall -Wextra $(XEMU_DEFS) \
	  $(XEMU_SRC) xemu/main.c -o $@ -lm

xemu-bin: build/xemu

# Static decoder coverage scan over a kernel image (see tools/isa_scan.py).
build/xemu_isa_scan: $(XEMU_SRC) xemu/isa_scan.c xemu/xemu.h xemu/ops.h
	@mkdir -p build
	/usr/bin/clang -isysroot $(SDK) -std=c11 -O2 -Wall -Wextra $(XEMU_DEFS) \
	  $(XEMU_SRC) xemu/isa_scan.c -o $@ -lm

.PHONY: xemu-isa-scan
xemu-isa-scan: build/xemu_isa_scan
	python3 tools/isa_scan.py $(or $(IMAGE),build/linux/vmlinux)

# T4 gate: real Linux boots to userspace and runs /init (see xemu/docs/linux-testplan.md).
# Artifacts come from tools/linux/build_kernel.sh + tools/linux/mkinitramfs.py.
.PHONY: xemu-linux-test
# The kernel is linked at physical 0x1000000 and needs >26 MiB of guest RAM.
# Guest memory is a build-time constant, so the Linux runner is a separate
# binary with the bigger -DXEMU_MEM_SIZE_MB instead of flipping build/xemu.
XEMU_LINUX_MEM_MB ?= 512
build/xemu-linux: $(XEMU_SRC) xemu/main.c xemu/xemu.h xemu/ops.h
	@mkdir -p build
	/usr/bin/clang -isysroot $(SDK) -std=c11 -O2 -Wall -Wextra \
	  -DXEMU_MEM_SIZE_MB=$(XEMU_LINUX_MEM_MB) $(XEMU_SRC) xemu/main.c -o $@ -lm

.PHONY: xemu-linux-runner
xemu-linux-runner: build/xemu-linux

.PHONY: xemu-linux-test
xemu-linux-test: build/xemu-linux
	python3 tools/linux/boot_test.py --xemu build/xemu-linux

.PHONY: isa-diff
isa-diff: xemu-so
	python3 tools/isa_diff.py 100 1

# L0 device tests (port I/O, UART, PIC, PIT) plus the P3 system tests.
.PHONY: xemu-dev-test
xemu-dev-test: xemu-bin
	python3 tools/xemu_sys.py

xemu-test: emu xemu-so
	python3 tools/difftest.py tests/emu/xemu_m0.S
	./build/emu -i build/tests/xemu_m0.bin --ref-so=$(XEMU_SO)

XEMU_DECODE_TEST := build/xemu_decode_test
XEMU_FIELD_TEST := build/xemu_decode_field_test

$(XEMU_DECODE_TEST): xemu/decode.c xemu/decode_test.c xemu/mem.c xemu/state.c $(wildcard xemu/dev/*.c)
	/usr/bin/clang -isysroot $(SDK) -std=c11 -O2 -Wall -Wextra \
	  xemu/decode.c xemu/mem.c xemu/state.c $(wildcard xemu/dev/*.c) \
	  xemu/decode_test.c -o $@

$(XEMU_FIELD_TEST): xemu/decode.c xemu/decode_field_test.c xemu/mem.c xemu/state.c $(wildcard xemu/dev/*.c)
	/usr/bin/clang -isysroot $(SDK) -std=c11 -O2 -Wall -Wextra \
	  xemu/decode.c xemu/mem.c xemu/state.c $(wildcard xemu/dev/*.c) \
	  xemu/decode_field_test.c -o $@

.PHONY: xemu-regress xemu-decode-test
xemu-regress: emu xemu-so
	python3 tools/xemu_regress.py

xemu-decode-test: decode-sim decode-vectors $(XEMU_DECODE_TEST) $(XEMU_FIELD_TEST)
	DUMP=1 $(DEC_OBJ)/decoder_tb $(DEC_DIR)/vectors.txt $(DEC_DIR)/image.bin > $(DEC_DIR)/dump.txt
	$(XEMU_DECODE_TEST) $(DEC_DIR)/vectors.txt $(DEC_DIR)/image.bin
	$(XEMU_FIELD_TEST) $(DEC_DIR)/dump.txt $(DEC_DIR)/image.bin

clean:
	rm -rf build target project/target project/project

# ---------------------------------------------------------------------------
# Interactive busybox shell on the serial console (T5 in
# xemu/docs/linux-testplan.md): boot a shell initramfs, type real commands and
# check their output.  The static busybox is downloaded to build/ (gitignored)
# and the initramfs is generated by mkinitramfs.py --shell; neither is
# committed.  This block is additive: it does not change any target above.
# ---------------------------------------------------------------------------
BUSYBOX ?= build/linux/busybox
INITRAMFS_SHELL ?= build/linux/initramfs-shell.cpio.gz

$(BUSYBOX):
	tools/linux/fetch_busybox.sh --out $@

$(INITRAMFS_SHELL): tools/linux/mkinitramfs.py $(BUSYBOX)
	python3 tools/linux/mkinitramfs.py --shell --busybox $(BUSYBOX) --out $@

.PHONY: xemu-shell-test
xemu-shell-test: build/xemu-linux $(INITRAMFS_SHELL)
	python3 tools/linux/shell_test.py --xemu build/xemu-linux --initrd $(INITRAMFS_SHELL)

# L4 acceptance: the shell must stay responsive for MINUTES (default 10) with
# no hang and no kernel complaint.  This is the plan's "连续运行 10 分钟无 hang".
MINUTES ?= 10
.PHONY: xemu-soak-test
xemu-soak-test: build/xemu-linux $(INITRAMFS_SHELL)
	python3 tools/linux/soak_test.py --xemu build/xemu-linux \
	  --initrd $(INITRAMFS_SHELL) --minutes $(MINUTES) --interval 8
