/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 tongxiaojun
 */
/* Linux x86 64-bit boot protocol loader for the standalone runner.
 *
 * The bzImage's protected-mode payload is placed at its `code32_start`
 * address, a zero page (boot_params) is built at 0x10000, the command line
 * lives at 0x20000, and control transfers to the decompressor's 64-bit entry
 * (payload + 0x200) with %rsi pointing at the zero page.
 */
#include "xemu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ZERO_PAGE 0x10000ull
#define CMDLINE_ADDR 0x20000ull
#define INITRD_ADDR 0x8000000ull

#define O_SETUP_SECTS 0x1f1
#define O_BOOT_FLAG 0x1fe
#define O_HEADER 0x202
#define O_VERSION 0x206
#define O_TYPE_LOADER 0x210
#define O_LOADFLAGS 0x211
#define O_CODE32_START 0x214
#define O_RAMDISK_IMG 0x218
#define O_RAMDISK_SIZE 0x21c
#define O_CMDLINE_PTR 0x228
#define O_INITRD_MAX 0x22c
#define O_KERNEL_ALIGN 0x230
#define O_RELOCATABLE 0x234
#define O_XLOADFLAGS 0x236
#define O_CMDLINE_SIZE 0x238
#define O_INIT_SIZE 0x260
#define O_E820_ENTRIES 0x1e8
#define O_E820_TABLE 0x2d0

static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static void wr32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

/* Build the zero page (boot_params) at ZERO_PAGE and the command line.
 *
 * Both entry paths need this: ELF vmlinux jumps straight to startup_64, and
 * the bzImage path also needs the setup header fields. `src` is the bzImage
 * header (may be NULL for a pre-decompressed vmlinux) and `load_addr` the
 * address the kernel image was placed at.
 */
static uint64_t initrd_addr, initrd_size;

static void write_boot_params(const uint8_t *src, uint64_t load_addr,
                              const char *cmdline) {
  uint8_t zp[4096];
  memset(zp, 0, sizeof(zp));

  wr16(zp + O_BOOT_FLAG, 0xAA55);
  memcpy(zp + O_HEADER, "HdrS", 4);
  zp[O_SETUP_SECTS] = (src && src[O_SETUP_SECTS]) ? src[O_SETUP_SECTS] : 4;
  zp[O_TYPE_LOADER] = 0xFF;                    /* undefined loader */
  zp[O_LOADFLAGS] = src ? (uint8_t)(src[O_LOADFLAGS] | 0x80) : 0x81;
  if (src) {
    wr16(zp + O_VERSION, rd16(src + O_VERSION));
    wr32(zp + O_KERNEL_ALIGN, rd32(src + O_KERNEL_ALIGN));
    zp[O_RELOCATABLE] = src[O_RELOCATABLE];
    wr16(zp + O_XLOADFLAGS, rd16(src + O_XLOADFLAGS));
    wr32(zp + O_INIT_SIZE, rd32(src + O_INIT_SIZE));
  } else {
    wr16(zp + O_VERSION, 0x020f);
    wr32(zp + O_KERNEL_ALIGN, 0x200000);
    zp[O_RELOCATABLE] = 1;
    wr16(zp + O_XLOADFLAGS, 0x7f);
  }
  wr32(zp + O_CODE32_START, (uint32_t)load_addr);
  wr32(zp + O_CMDLINE_PTR, (uint32_t)CMDLINE_ADDR);
  wr32(zp + O_CMDLINE_SIZE, 2048);
  wr32(zp + O_INITRD_MAX, 0x37FFFFFF);
  if (initrd_size) {
    wr32(zp + O_RAMDISK_IMG, (uint32_t)initrd_addr);
    wr32(zp + O_RAMDISK_SIZE, (uint32_t)initrd_size);
  }

  struct { uint64_t addr, size; uint32_t type; } map[] = {
    {0x0, 0x9FC00, 1},
    {0x9FC00, 0xA0000 - 0x9FC00, 2},
    {0xF0000, 0x100000 - 0xF0000, 2},
    {0x100000, XEMU_MEM_SIZE - 0x100000, 1},
  };
  int nent = (int)(sizeof(map) / sizeof(map[0]));
  zp[O_E820_ENTRIES] = (uint8_t)nent;
  uint8_t *p = zp + O_E820_TABLE;
  for (int i = 0; i < nent; i++) {
    for (int b = 0; b < 8; b++) p[b] = (uint8_t)(map[i].addr >> (8 * b));
    for (int b = 0; b < 8; b++) p[8 + b] = (uint8_t)(map[i].size >> (8 * b));
    wr32(p + 16, map[i].type);
    p += 20;
  }

  xemu_mem_write(ZERO_PAGE, zp, sizeof(zp));
  xemu_mem_write(CMDLINE_ADDR, cmdline, strlen(cmdline) + 1);
}

/* Build the 4-level identity map the kernel's early boot expects and turn on
 * long mode: PML4[0] -> PDPT, PDPT[0] -> PD, PD[0..N] = 2MiB pages covering
 * all of RAM, plus a scratch page at the halt-MMIO address so a stray store
 * does not fault. The kernel builds its own tables later and reloads CR3. */
#define PT_PML4 0x1000ull
#define PT_PDPT 0x2000ull
#define PT_PD   0x3000ull

static int setup_long_mode(void) {
  memset(&xemu_mem[PT_PML4], 0, 3 * 4096);
  uint64_t pml4e = PT_PDPT | 3;
  uint64_t pdpte = PT_PD | 3;
  xemu_mem_write(PT_PML4, &pml4e, 8);
  xemu_mem_write(PT_PDPT, &pdpte, 8);
  uint64_t pages = XEMU_MEM_SIZE >> 21;          /* 2MiB pages */
  if (pages > 512) pages = 512;                  /* one PD covers 1 GiB */
  for (uint64_t i = 0; i < pages; i++) {
    uint64_t e = (i << 21) | 0x83;               /* present, RW, PS */
    xemu_mem_write(PT_PD + i * 8, &e, 8);
  }
  xemu.cr3 = PT_PML4;
  xemu.cr4 |= 0x20;                              /* PAE */
  xemu.efer |= 0x500;                            /* LME | LMA */
  xemu.cr0 |= 0x80000000ull | 1ull;              /* PG | PE */
  return 0;
}

/* Minimal ELF64 loader: maps PT_LOAD segments at their physical (p_paddr,
 * falling back to p_vaddr) addresses. Used for pre-decompressed vmlinux
 * images, which skip the in-guest decompressor entirely. */
static int load_elf(const uint8_t *img, long n, uint64_t *entry) {
  if (n < 64 || memcmp(img, "\177ELF", 4) != 0) return -1;
  if (img[4] != 2) { fprintf(stderr, "ELF is not 64-bit\n"); return -1; }
  uint64_t phoff = 0; uint16_t phentsize = 0, phnum = 0;
  for (int b = 0; b < 8; b++) phoff |= (uint64_t)img[0x20 + b] << (8 * b);
  for (int b = 0; b < 2; b++) phentsize |= (uint16_t)img[0x36 + b] << (8 * b);
  for (int b = 0; b < 2; b++) phnum |= (uint16_t)img[0x38 + b] << (8 * b);
  for (int b = 0; b < 8; b++) *entry |= (uint64_t)img[0x18 + b] << (8 * b);
  for (int i = 0; i < phnum; i++) {
    const uint8_t *ph = img + phoff + (uint64_t)i * phentsize;
    uint32_t type = rd32(ph);
    if (type != 1) continue;                     /* PT_LOAD only */
    uint64_t p_offset = 0, p_vaddr = 0, p_paddr = 0, p_filesz = 0;
    for (int b = 0; b < 8; b++) {
      p_offset |= (uint64_t)ph[0x08 + b] << (8 * b);
      p_vaddr  |= (uint64_t)ph[0x10 + b] << (8 * b);
      p_paddr  |= (uint64_t)ph[0x18 + b] << (8 * b);
      p_filesz |= (uint64_t)ph[0x20 + b] << (8 * b);
    }
    /* Prefer the physical address. Reconstructed vmlinux ELFs (kallsyms-only
     * images) set p_paddr = p_vaddr = the kernel virtual address; map those
     * back through the kernel image window (__START_KERNEL_map). */
    uint64_t dest = p_paddr;
    if (dest == 0 || dest >= 0xffffffff80000000ull)
      dest = p_vaddr >= 0xffffffff80000000ull ? p_vaddr - 0xffffffff80000000ull : p_vaddr;
    if (dest + p_filesz > XEMU_MEM_SIZE) {
      fprintf(stderr, "xemu: segment 0x%llx+0x%llx exceeds RAM (%llu MiB)\n",
              (unsigned long long)dest, (unsigned long long)p_filesz,
              (unsigned long long)(XEMU_MEM_SIZE >> 20));
      return -1;
    }
    if (xemu_mem_write(dest, img + p_offset, p_filesz) != 0) return -1;
  }
  /* Same normalisation for the entry point: start at its physical alias so
   * the identity map installed by the loader is in effect. */
  if (*entry >= 0xffffffff80000000ull) *entry -= 0xffffffff80000000ull;
  return 0;
}

/* Place an initramfs image high in RAM, below the 4 GiB line the boot
 * protocol uses for 32-bit pointers. */
static int load_initrd(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "xemu: cannot open initrd %s\n", path); return -1; }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n <= 0) { fclose(f); return -1; }
  uint64_t addr = (XEMU_MEM_SIZE - (uint64_t)n - 0x100000ull) & ~0xfffull;
  uint8_t *buf = malloc((size_t)n);
  if (!buf) { fclose(f); return -1; }
  if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return -1; }
  fclose(f);
  if (xemu_mem_write(addr, buf, (uint64_t)n) != 0) { free(buf); return -1; }
  free(buf);
  initrd_addr = addr;
  initrd_size = (uint64_t)n;
  printf("xemu: initrd %ld bytes -> 0x%llx\n", n, (unsigned long long)addr);
  return 0;
}

int xemu_boot_kernel_initrd(const char *path, const char *cmdline,
                            const char *initrd);

int xemu_boot_kernel(const char *path, const char *cmdline) {
  return xemu_boot_kernel_initrd(path, cmdline, NULL);
}

int xemu_boot_kernel_initrd(const char *path, const char *cmdline,
                            const char *initrd) {
  if (initrd && load_initrd(initrd) != 0) return -1;
  FILE *f = fopen(path, "rb");
  if (!f) { perror(path); return -1; }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  rewind(f);
  uint8_t *img = malloc((size_t)n);
  if (!img || fread(img, 1, (size_t)n, f) != (size_t)n) {
    fclose(f); free(img); fprintf(stderr, "read failed\n"); return -1;
  }
  fclose(f);

  /* A pre-decompressed vmlinux takes the ELF path: no in-guest decompressor,
     which at interpreter speed would cost billions of instructions. */
  if (n > 4 && memcmp(img, "\177ELF", 4) == 0) {
    uint64_t entry = 0;
    if (load_elf(img, n, &entry) != 0) { free(img); return -1; }
    write_boot_params(NULL, entry, cmdline);
    xemu.gpr[6] = ZERO_PAGE;          /* %rsi = boot_params */
    xemu.gpr[4] = 0x9F000;
    xemu.rip = entry;
    /* Linux's startup_64 expects to run with long mode already active and
     * the identity mapping installed by the boot loader: it only programs
     * CR4 and CR3, never CR0.PG. Build that state here (see setup_long_mode). */
    if (setup_long_mode() != 0) { free(img); return -1; }
    printf("xemu: loaded ELF vmlinux, entry 0x%llx\n", (unsigned long long)entry);
    free(img);
    return 0;
  }
  if (n < 0x300 || rd16(img + O_BOOT_FLAG) != 0xAA55 ||
      memcmp(img + O_HEADER, "HdrS", 4) != 0) {
    fprintf(stderr, "not a boot-protocol or ELF image\n");
    free(img);
    return -1;
  }
  uint8_t setup_sects = img[O_SETUP_SECTS] ? img[O_SETUP_SECTS] : 4;
  uint64_t payload_off = ((uint64_t)setup_sects + 1) * 512;
  uint32_t load_addr = rd32(img + O_CODE32_START);
  uint16_t xloadflags = rd16(img + O_XLOADFLAGS);
  uint32_t init_size = rd32(img + O_INIT_SIZE);
  int is64 = (xloadflags & 1) != 0;
  uint64_t entry = load_addr + (is64 ? 0x200 : 0);
  uint64_t payload_len = (uint64_t)n - payload_off;

  printf("xemu: kernel version=0x%x loadflags=0x%x xloadflags=0x%x\n",
         rd16(img + O_VERSION), img[O_LOADFLAGS], xloadflags);
  printf("xemu: payload %llu bytes -> 0x%x, entry 0x%llx (%s)\n",
         (unsigned long long)payload_len, load_addr,
         (unsigned long long)entry, is64 ? "64-bit" : "32-bit");

  if (load_addr + payload_len + init_size > XEMU_MEM_SIZE) {
    fprintf(stderr,
            "xemu: kernel needs ~%llu MiB but RAM is %llu MiB; rebuild with "
            "-DXEMU_MEM_SIZE_MB=512\n",
            (unsigned long long)((load_addr + init_size) >> 20),
            (unsigned long long)(XEMU_MEM_SIZE >> 20));
    free(img);
    return -1;
  }
  if (xemu_mem_write(load_addr, img + payload_off, payload_len) != 0) {
    fprintf(stderr, "xemu: payload does not fit in RAM\n");
    free(img);
    return -1;
  }

  write_boot_params(img, load_addr, cmdline);

  xemu.gpr[6] = ZERO_PAGE;                          /* %rsi = boot_params */
  xemu.gpr[4] = 0x9F000;                            /* stack below the EBDA */
  xemu.rip = entry;
  printf("xemu: boot_params at 0x%llx, cmdline \"%s\"\n",
         (unsigned long long)ZERO_PAGE, cmdline);
  free(img);
  return 0;
}
