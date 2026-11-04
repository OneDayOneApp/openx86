#include "xemu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <vectors.txt> <image.bin>\n", argv[0]);
    return 2;
  }
  FILE *img = fopen(argv[2], "rb");
  if (!img) { perror("image"); return 2; }
  static uint8_t image[1 << 20];
  size_t n = fread(image, 1, sizeof(image), img);
  fclose(img);
  xemu_reset();
  if (xemu_mem_write(0x400000, image, n) != 0) {
    fprintf(stderr, "image too large\n");
    return 2;
  }
  FILE *vec = fopen(argv[1], "r");
  if (!vec) { perror("vectors"); return 2; }
  unsigned long long pc;
  unsigned len;
  char name[512];
  int total = 0, fail = 0;
  while (fscanf(vec, "%llx %u %511s", &pc, &len, name) == 3) {
    XemuInst inst;
    total++;
    int rc = xemu_decode(pc, &inst);
    if (rc != 0 || inst.len != len || inst.illegal) {
      fail++;
      if (fail <= 30)
        printf("FAIL %-40s pc=%llx exp_len=%u got_len=%u rc=%d illegal=%d\n",
               name, pc, len, inst.len, rc, inst.illegal);
    }
  }
  fclose(vec);
  printf("xemu decode: %d/%d passed, %d failed\n", total - fail, total, fail);
  return fail ? 1 : 0;
}
