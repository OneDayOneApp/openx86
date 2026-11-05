/* Static decoder coverage scanner.
 *
 * Reads `ADDR HEXBYTES [MNEMONIC]` lines (produced by tools/isa_scan.py from
 * `llvm-objdump` output), decodes each instruction with the real XEMU decoder
 * and reports the ones it rejects. Used to find every instruction in a kernel
 * image that XEMU cannot execute, without having to boot it one crash at a
 * time.
 */
#include "xemu.h"
#include "ops.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCRATCH 0x100000ull

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s INSTR-FILE\n", argv[0]);
    return 2;
  }
  FILE *f = fopen(argv[1], "r");
  if (!f) { perror(argv[1]); return 2; }
  xemu_reset();
  xemu.cr0 = 0;                 /* no paging: virtual == physical */

  char line[512];
  unsigned long long total = 0, unsupported = 0;
  while (fgets(line, sizeof(line), f)) {
    unsigned long long addr = 0;
    char hex[64] = {0};
    char mnem[256] = {0};
    int n = sscanf(line, "%llx %63s %255[^\n]", &addr, hex, mnem);
    if (n < 2) continue;
    size_t len = strlen(hex) / 2;
    if (len == 0 || len > 15) continue;
    uint8_t b[16];
    for (size_t i = 0; i < len; i++) {
      unsigned v = 0;
      if (sscanf(hex + 2 * i, "%2x", &v) != 1) { len = 0; break; }
      b[i] = (uint8_t)v;
    }
    if (!len) continue;
    /* Pad with NOPs so the decoder's 16-byte read stays in bounds. */
    memset(b + len, 0x90, 16 - len);
    xemu_mem_write(SCRATCH, b, 16);
    xemu.rip = SCRATCH;
    XemuInst inst;
    memset(&inst, 0, sizeof(inst));
    int rc = xemu_decode(SCRATCH, &inst);
    total++;
    /* Only a real failure counts.  `op >= 0xff` used to be a stand-in for
       "unassigned", but the XEMU-only expansion now numbers real opcodes from
       300 up (and FXSAVE from 260), so that test flagged working instructions
       such as BSWAP (311) and PAUSE. */
    if (rc != 0 || inst.op == Ops_UNDEF || inst.illegal) {
      unsupported++;
      printf("%llx %s %s\n", addr, hex, mnem);
    }
  }
  fclose(f);
  fprintf(stderr, "scanned %llu instructions, %llu unsupported\n", total, unsupported);
  return unsupported != 0;
}
