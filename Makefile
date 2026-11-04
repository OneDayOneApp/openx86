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
XEMU_SRC := $(filter-out xemu/decode_test.c xemu/decode_field_test.c,$(wildcard xemu/*.c))

.PHONY: xemu-so
xemu-so: $(XEMU_SO)

$(XEMU_SO): $(XEMU_SRC) xemu/xemu.h ref/difftest_abi.h
	@mkdir -p build/ref
	/usr/bin/clang -isysroot $(SDK) -std=c11 -O2 -fPIC -shared \
	  -Wall -Wextra -o $@ $(XEMU_SRC) -lm

xemu-test: emu xemu-so
	python3 tools/difftest.py tests/emu/xemu_m0.S
	./build/emu -i build/tests/xemu_m0.bin --ref-so=$(XEMU_SO)

XEMU_DECODE_TEST := build/xemu_decode_test
XEMU_FIELD_TEST := build/xemu_decode_field_test

$(XEMU_DECODE_TEST): xemu/decode.c xemu/decode_test.c xemu/mem.c xemu/state.c
	/usr/bin/clang -isysroot $(SDK) -std=c11 -O2 -Wall -Wextra \
	  xemu/decode.c xemu/mem.c xemu/state.c xemu/decode_test.c -o $@

$(XEMU_FIELD_TEST): xemu/decode.c xemu/decode_field_test.c xemu/mem.c xemu/state.c
	/usr/bin/clang -isysroot $(SDK) -std=c11 -O2 -Wall -Wextra \
	  xemu/decode.c xemu/mem.c xemu/state.c xemu/decode_field_test.c -o $@

.PHONY: xemu-regress xemu-decode-test
xemu-regress: emu xemu-so
	python3 tools/xemu_regress.py

xemu-decode-test: decode-sim decode-vectors $(XEMU_DECODE_TEST) $(XEMU_FIELD_TEST)
	DUMP=1 $(DEC_OBJ)/decoder_tb $(DEC_DIR)/vectors.txt $(DEC_DIR)/image.bin > $(DEC_DIR)/dump.txt
	$(XEMU_DECODE_TEST) $(DEC_DIR)/vectors.txt $(DEC_DIR)/image.bin
	$(XEMU_FIELD_TEST) $(DEC_DIR)/dump.txt $(DEC_DIR)/image.bin

clean:
	rm -rf build target project/target project/project
