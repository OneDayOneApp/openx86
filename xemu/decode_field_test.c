#include "xemu.h"
#include "ops.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <dump.txt> <image.bin>\n", argv[0]);
    return 2;
  }
  FILE *img = fopen(argv[2], "rb");
  if (!img) { perror("image"); return 2; }
  static uint8_t image[1 << 20];
  size_t n = fread(image, 1, sizeof(image), img);
  fclose(img);
  xemu_reset();
  xemu_mem_write(0x400000, image, n);

  FILE *f = fopen(argv[1], "r");
  if (!f) { perror("dump"); return 2; }
  char line[1024];
  int total = 0, fail = 0;
  int bad_len=0, bad_op=0, bad_simd=0, bad_sz=0, bad_msz=0, bad_a=0, bad_b=0,
      bad_imm=0, bad_base=0, bad_idx=0, bad_disp=0, bad_illegal=0;
  while (fgets(line, sizeof(line), f)) {
    if (line[0] != 'D' || line[1] != ' ') continue;
    char name[512];
    unsigned long long pc, imm;
    unsigned len, op, simd, sz, msz, ak, ai, bk, bi, base, idx;
    long long disp;
    int got = sscanf(line,
      "D %511s pc=%llx len=%u op=%u simd=%u sz=%u msz=%u a=%u/%u b=%u/%u imm=%llx base=%u idx=%u disp=%lld",
      name, &pc, &len, &op, &simd, &sz, &msz, &ak, &ai, &bk, &bi, &imm,
      &base, &idx, &disp);
    if (got != 15) continue;
    XemuInst d;
    int rc = xemu_decode(pc, &d);
    total++;
    int bad = 0;
    if (rc != 0 || d.len != len) { bad_len++; bad = 1; }
    if (rc == 0 && d.op != op) { bad_op++; bad = 1; }
    if (rc == 0 && d.simd != simd) { bad_simd++; bad = 1; }
    if (rc == 0 && d.opsz != sz) { bad_sz++; bad = 1; }
    if (rc == 0 && d.memsz != msz) { bad_msz++; bad = 1; }
    /* The register index of an immediate operand is a don't-care: the RTL
       leaves the byte it read as ModRM in b_idx for the accumulator-immediate
       forms (e.g. "subb $0x11,%al" reports b_idx=1), XEMU reports 0. Compare
       the index only for register/memory operands. */
    if (rc == 0 && (d.a_kind != ak || (d.a_kind != XK_IMM && d.a_idx != ai))) { bad_a++; bad = 1; }
    if (rc == 0 && (d.b_kind != bk || (d.b_kind != XK_IMM && d.b_idx != bi))) { bad_b++; bad = 1; }
    if (rc == 0 && d.imm != imm) { bad_imm++; bad = 1; }
    if (rc == 0 && (unsigned)d.base != base) { bad_base++; bad = 1; }
    if (rc == 0 && (unsigned)d.index != idx) { bad_idx++; bad = 1; }
    if (rc == 0 && d.disp != disp) { bad_disp++; bad = 1; }
    if (rc == 0 && d.illegal) { bad_illegal++; bad = 1; }
    if (bad) {
      fail++;
      if (fail <= 25)
        printf("FAIL %-32s pc=%llx len %u/%u op %u/%u simd %u/%u sz %u/%u msz %u/%u a %u/%u %u/%u b %u/%u %u/%u imm %llx/%llx base %u/%u idx %u/%u disp %lld/%lld\n",
               name, pc, len, d.len, op, d.op, simd, d.simd, sz, d.opsz, msz, d.memsz,
               ak, d.a_kind, ai, d.a_idx, bk, d.b_kind, bi, d.b_idx,
               imm, (unsigned long long)d.imm, base, (unsigned)d.base, idx, (unsigned)d.index,
               disp, (long long)d.disp);
    }
  }
  fclose(f);
  printf("xemu decode fields: %d/%d passed, %d failed\n", total - fail, total, fail);
  printf("by field: len=%d op=%d simd=%d sz=%d msz=%d a=%d b=%d imm=%d base=%d idx=%d disp=%d illegal=%d\n",
         bad_len, bad_op, bad_simd, bad_sz, bad_msz, bad_a, bad_b, bad_imm,
         bad_base, bad_idx, bad_disp, bad_illegal);
  return fail ? 1 : 0;
}
