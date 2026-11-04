#include "xemu.h"

/* M0 decoder: legacy prefixes, REX, MOV r64, imm64 (REX.W B8+rd) and HLT.
 * M1 will replace this with the full table-driven decoder. */
int xemu_decode(uint64_t rip, XemuInst *out) {
  uint8_t buf[16];
  if (xemu_mem_read(rip, buf, sizeof(buf)) != 0) return -1;

  size_t i = 0;
  int rex_w = 0;
  uint8_t rex = 0;
  int p66 = 0;
  for (;;) {
    uint8_t b = buf[i];
    if (b == 0x66) { p66 = 1; i++; continue; }
    if (b == 0x67 || b == 0xF0 || b == 0xF2 || b == 0xF3) { i++; continue; }
    if (b >= 0x40 && b <= 0x4F) { rex = b; rex_w = (b >> 3) & 1; i++; continue; }
    break;
  }

  uint8_t op = buf[i++];
  out->a_kind = XK_NONE; out->b_kind = XK_NONE;
  out->a_idx = out->b_idx = 0;
  out->imm = 0; out->disp = 0;
  out->base = out->index = 0; out->scale = 0; out->rip_rel = 0;

  if (op >= 0xB8 && op <= 0xBF) {
    out->op = XOP_MOV;
    out->a_kind = XK_GPR;
    out->a_idx = (op - 0xB8) | ((rex & 1) ? 8 : 0);
    if (rex_w) {
      if (xemu_mem_read(rip + i, &out->imm, 8) != 0) return -2;
      i += 8;
      out->opsz = 8;
    } else {
      uint32_t v;
      if (xemu_mem_read(rip + i, &v, 4) != 0) return -2;
      out->imm = v;
      i += 4;
      out->opsz = 4;
    }
    out->len = (uint8_t)i;
    return 0;
  }
  if (op == 0xF4) {
    out->op = XOP_HLT;
    out->len = (uint8_t)i;
    out->opsz = 0;
    return 0;
  }
  (void)p66;
  return -3;
}
