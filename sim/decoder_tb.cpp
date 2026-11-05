/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 tongxiaojun
 */
#include "VDecoder.h"
#include "verilated.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static void set_bytes(VDecoder &d, const unsigned char *b) {
  d.io_bytes_0 = b[0];
  d.io_bytes_1 = b[1];
  d.io_bytes_2 = b[2];
  d.io_bytes_3 = b[3];
  d.io_bytes_4 = b[4];
  d.io_bytes_5 = b[5];
  d.io_bytes_6 = b[6];
  d.io_bytes_7 = b[7];
  d.io_bytes_8 = b[8];
  d.io_bytes_9 = b[9];
  d.io_bytes_10 = b[10];
  d.io_bytes_11 = b[11];
  d.io_bytes_12 = b[12];
  d.io_bytes_13 = b[13];
  d.io_bytes_14 = b[14];
  d.io_bytes_15 = b[15];
}

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <vectors.txt> <image.bin>\n", argv[0]);
    return 2;
  }

  std::ifstream img(argv[2], std::ios::binary);
  std::vector<unsigned char> mem((std::istreambuf_iterator<char>(img)),
                                 std::istreambuf_iterator<char>());
  std::ifstream vec(argv[1]);
  if (!img || !vec) {
    std::fprintf(stderr, "cannot open inputs\n");
    return 2;
  }

  VDecoder dut;
  dut.clock = 0;
  dut.reset = 0;

  const uint64_t base = 0x400000;
  uint64_t pc = 0;
  unsigned len = 0;
  char name[512];
  int total = 0, fail = 0;

  while (vec >> std::hex >> pc >> std::dec >> len >> name) {
    size_t off = (size_t)(pc - base);
    unsigned char bytes[16];
    for (int i = 0; i < 16; i++)
      bytes[i] = (off + i < mem.size()) ? mem[off + i] : 0xcc;

    dut.io_pc = pc;
    set_bytes(dut, bytes);
    dut.eval();

    total++;
    if (getenv("DUMP"))
      std::printf("D %-34s pc=%llx len=%u op=%u simd=%u sz=%u msz=%u a=%u/%u b=%u/%u imm=%llx base=%u idx=%u disp=%lld\n",
                  name, (unsigned long long)pc, (unsigned)dut.io_len, (unsigned)dut.io_uop_op, (unsigned)dut.io_uop_simd, (unsigned)dut.io_uop_opsz, (unsigned)dut.io_uop_memsz,
                  (unsigned)dut.io_uop_a_kind, (unsigned)dut.io_uop_a_idx,
                  (unsigned)dut.io_uop_b_kind, (unsigned)dut.io_uop_b_idx,
                  (unsigned long long)dut.io_uop_imm, (unsigned)dut.io_uop_mem_base,
                  (unsigned)dut.io_uop_mem_index, (long long)dut.io_uop_mem_disp);
    bool bad = ((unsigned)dut.io_len != len) || dut.io_illegal;
    if (bad) {
      fail++;
      if (fail <= 30) {
        std::printf("FAIL %-40s pc=%08llx exp_len=%u got_len=%u illegal=%d\n",
                    name, (unsigned long long)pc, len, (unsigned)dut.io_len,
                    (int)dut.io_illegal);
      }
    }
  }

  std::printf("decode: %d/%d passed, %d failed\n", total - fail, total, fail);
  return fail ? 1 : 0;
}
