#include "xemu.h"
#include <stdio.h>
#include <string.h>

int xemu_mem_read(uint64_t addr, void *buf, size_t n) {
  if (addr + n <= XEMU_MEM_SIZE) {
    memcpy(buf, &xemu_mem[addr], n);
    return 0;
  }
  if (addr >= XEMU_MMIO_HALT && addr + n <= XEMU_MMIO_HALT + XEMU_MMIO_SIZE) {
    memset(buf, 0, n);
    return 0;
  }
  return -1;
}

int xemu_mem_write(uint64_t addr, const void *buf, size_t n) {
  if (addr + n <= XEMU_MEM_SIZE) {
    memcpy(&xemu_mem[addr], buf, n);
    return 0;
  }
  if (addr >= XEMU_MMIO_HALT && addr + n <= XEMU_MMIO_HALT + XEMU_MMIO_SIZE) {
    /* Halt MMIO: any write stops the reference model; the low byte is the exit code. */
    uint64_t v = 0;
    const uint8_t *b = (const uint8_t *)buf;
    for (size_t i = 0; i < n && i < 8; i++) v |= (uint64_t)b[i] << (8 * i);
    xemu_exit_code = v;
    xemu.halted = 1;
    return 0;
  }
  return -1;
}

/* ---------------------------------------------------------------- paging */
int xemu_translate(uint64_t va, int write, int fetch, uint64_t *pa, uint64_t *errcode) {
  uint64_t err = 0;
  if (write) err |= 2;
  if (fetch) err |= 16;
  if (!(xemu.cr0 & 0x80000000ull)) { *pa = va; *errcode = 0; return 0; }

  uint64_t table = xemu.cr3 & ~0xfffull;
  for (int level = 0; level < 4; level++) {
    uint64_t idx = (va >> (12 + 9 * (3 - level))) & 0x1ff;
    uint64_t e;
    if (xemu_mem_read(table + idx * 8, &e, 8) != 0) { *errcode = err; return -1; }
    if (!(e & 1)) { *errcode = err; return -1; }
    if (write && !(e & 2)) { *errcode = err | 1; return -1; }
    if (fetch && (e & (1ull << 63)) && (xemu.efer & 0x800)) { *errcode = err | 1; return -1; }
    if (level == 1 && (e & 0x80)) { /* 1 GiB */
      *pa = (e & 0xfffffc0000000ull) + (va & 0x3fffffffull);
      *errcode = 0; return 0;
    }
    if (level == 2 && (e & 0x80)) { /* 2 MiB */
      *pa = (e & 0xfffffffe00000ull) + (va & 0x1fffffull);
      *errcode = 0; return 0;
    }
    if (level == 3) {
      *pa = (e & ~0xfffull) + (va & 0xfffull);
      *errcode = 0; return 0;
    }
    table = e & ~0xfffull;
  }
  *errcode = err;
  return -1;
}

uint64_t xemu_fault_err = 0;
uint64_t xemu_exit_code = 0;
int xemu_fault_pending = 0;

static int vaccess(uint64_t va, void *buf, size_t n, int write, int fetch) {
  uint8_t *p = (uint8_t *)buf;
  while (n) {
    uint64_t pa, err;
    if (xemu_translate(va, write, fetch, &pa, &err) != 0) {
      if (write || fetch) xemu.cr2 = va;
      xemu_fault_err = err;
      return -1;
    }
    size_t chunk = 0x1000 - (va & 0xfff);
    if (chunk > n) chunk = n;
    if (write ? xemu_mem_write(pa, p, chunk) : xemu_mem_read(pa, p, chunk))
      return -1;
    va += chunk; p += chunk; n -= chunk;
  }
  return 0;
}

int xemu_vread(uint64_t va, void *buf, size_t n) { return vaccess(va, buf, n, 0, 0); }
int xemu_vwrite(uint64_t va, const void *buf, size_t n) { return vaccess(va, (void *)buf, n, 1, 0); }

static int trap_has_err(uint64_t vec) {
  return vec == 8 || vec == 10 || vec == 11 || vec == 12 ||
         vec == 13 || vec == 14 || vec == 17 || vec == 21 ||
         vec == 29 || vec == 30;
}

void xemu_raise_exc(uint64_t vec, uint64_t err, uint64_t pc) {
  uint8_t gate[16];
  uint64_t base = xemu.idtr_base + vec * 16;
  if (xemu_mem_read(base, gate, 16) != 0) {
    fprintf(stderr, "[xemu] IDT read fault for vector %llu at 0x%llx\n",
            (unsigned long long)vec, (unsigned long long)base);
    xemu.halted = 1;
    return;
  }
  uint64_t low = 0, high = 0;
  for (int i = 0; i < 8; i++) {
    low |= (uint64_t)gate[i] << (8 * i);
    high |= (uint64_t)gate[8 + i] << (8 * i);
  }
  uint64_t handler = (low & 0xffff) | (((low >> 48) & 0xffff) << 16) |
                     ((high & 0xffffffffull) << 32);
  uint64_t sel = (low >> 16) & 0xffff;

  uint64_t old_rsp = xemu.gpr[4];
  uint64_t old_flags = xemu.rflags;
  xemu.gpr[4] = old_rsp - 8;
  uint64_t v = 0; xemu_mem_write(xemu.gpr[4], &v, 8);          /* SS */
  xemu.gpr[4] -= 8; xemu_mem_write(xemu.gpr[4], &old_rsp, 8);  /* RSP */
  xemu.gpr[4] -= 8; xemu_mem_write(xemu.gpr[4], &old_flags, 8);/* RFLAGS */
  v = 8; xemu.gpr[4] -= 8; xemu_mem_write(xemu.gpr[4], &v, 8); /* CS */
  xemu.gpr[4] -= 8; xemu_mem_write(xemu.gpr[4], &pc, 8);       /* RIP */
  if (trap_has_err(vec)) {
    xemu.gpr[4] -= 8; xemu_mem_write(xemu.gpr[4], &err, 8);
  }
  xemu.cs = sel;
  xemu.rflags = old_flags & ~(1ull << 9); /* interrupt gate clears IF */
  xemu.rip = handler;
}
