#ifndef XEMU_H
#define XEMU_H

#include <stddef.h>
#include <stdint.h>

#define XEMU_MEM_SIZE (16ull * 1024 * 1024)
#define XEMU_MMIO_HALT 0xF0000000ull
#define XEMU_MMIO_SIZE 0x1000ull

/* Operation ids aligned with the RTL Ops numbering where practical. */
enum {
  XOP_NONE = 0,
  XOP_MOV = 1,
  XOP_HLT = 41,
};

/* Operand kinds mirror the RTL decoder: None/Gpr/Xmm/Mem/Imm. */
enum { XK_NONE = 0, XK_GPR = 1, XK_XMM = 2, XK_MEM = 3, XK_IMM = 4 };

typedef struct {
  uint64_t gpr[16];
  uint64_t rip, rflags;
  uint64_t xmm[16][2];
  uint64_t cr0, cr2, cr3, cr4;
  uint64_t idtr_base;
  uint16_t idtr_limit;
  uint64_t efer, star, lstar, sfmask;
  uint64_t fs_base, gs_base;
  int halted;
} XemuState;

typedef struct {
  uint8_t len;
  uint16_t op;
  uint8_t opsz; /* 1/2/4/8 */
  uint8_t a_kind, b_kind;
  uint8_t a_idx, b_idx;
  uint64_t imm;
  int64_t disp;
  uint8_t base, index, scale;
  int rip_rel;
} XemuInst;

extern XemuState xemu;
extern uint8_t xemu_mem[XEMU_MEM_SIZE];

void xemu_reset(void);

int xemu_mem_read(uint64_t addr, void *buf, size_t n);
int xemu_mem_write(uint64_t addr, const void *buf, size_t n);

/* Returns 0 on success, negative on unsupported/invalid instruction. */
int xemu_decode(uint64_t rip, XemuInst *out);
/* Returns 0 on success, negative on fault. */
int xemu_exec(const XemuInst *inst);

#endif
