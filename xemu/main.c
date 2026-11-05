/* NEMU-style standalone runner/monitor for XEMU. */
#include "xemu.h"
#include "ops.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENTRY 0x400000ull

static long long max_steps = 100000000;
static const char *kernel_path = NULL;
static const char *cmdline = "console=ttyS0 earlyprintk=serial";
static unsigned mem_mb = 0;
static int batch = 0;
static int trace = 0;
static int trace_cr = 0;
static const char *dump_mem = NULL;
static unsigned long steps;

/* NEMU-style instruction ring buffer, dumped on a fatal error. */
static struct { uint64_t pc; uint16_t op; uint8_t len; } ring[16];
static unsigned ring_pos, ring_cnt;

static void ring_add(uint64_t pc, uint16_t op, uint8_t len) {
  ring[ring_pos].pc = pc; ring[ring_pos].op = op; ring[ring_pos].len = len;
  ring_pos = (ring_pos + 1) % 16;
  if (ring_cnt < 16) ring_cnt++;
}

static void ring_dump(void) {
  if (!ring_cnt) return;
  fprintf(stderr, "---- last %u instructions ----\n", ring_cnt);
  unsigned start = (ring_pos + 16 - ring_cnt) % 16;
  for (unsigned i = 0; i < ring_cnt; i++) {
    unsigned k = (start + i) % 16;
    fprintf(stderr, "  pc=0x%llx op=%u len=%u\n",
            (unsigned long long)ring[k].pc, (unsigned)ring[k].op, (unsigned)ring[k].len);
  }
}

static int load_image(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) { perror(path); return -1; }
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
  long n = ftell(f);
  if (n < 0 || (uint64_t)n > XEMU_MEM_SIZE) { fclose(f); fprintf(stderr, "image too large\n"); return -1; }
  rewind(f);
  static uint8_t buf[XEMU_MEM_SIZE];
  if (fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); fprintf(stderr, "read failed\n"); return -1; }
  fclose(f);
  if (xemu_mem_write(ENTRY, buf, (size_t)n) != 0) return -1;
  return 0;
}

static int step_once(void) {
  XemuInst inst;
  int rc = xemu_decode(xemu.rip, &inst);
  if (rc == 1) { steps++; return 0; } /* fetch fault delivered */
  if (rc != 0) {
    fprintf(stderr, "xemu: decode failed at rip=0x%llx (rc=%d)\n",
            (unsigned long long)xemu.rip, rc);
    return -1;
  }
  if (trace)
    fprintf(stderr, "0x%llx: op=%u len=%u\n",
            (unsigned long long)xemu.rip, (unsigned)inst.op, (unsigned)inst.len);
  if (trace_cr && (inst.op == Ops_MOV_CR || inst.op == Ops_SYSCALL))
    fprintf(stderr, "cr@0x%llx op=%u cr0=0x%llx cr3=0x%llx cr4=0x%llx rax=0x%llx\n",
            (unsigned long long)xemu.rip, inst.op,
            (unsigned long long)xemu.cr0, (unsigned long long)xemu.cr3,
            (unsigned long long)xemu.cr4, (unsigned long long)xemu.gpr[0]);
  ring_add(xemu.rip, inst.op, inst.len);
  rc = xemu_exec(&inst);
  if (rc != 0) {
    fprintf(stderr, "xemu: exec failed at rip=0x%llx op=%u\n",
            (unsigned long long)xemu.rip, (unsigned)inst.op);
    ring_dump();
    return -1;
  }
  steps++;
  return 0;
}

static void dump_regs(void) {
  static const char *names[16] = {"rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi",
                                  "r8","r9","r10","r11","r12","r13","r14","r15"};
  for (int i = 0; i < 16; i++)
    printf("%-3s = 0x%016llx%s", names[i], (unsigned long long)xemu.gpr[i], (i % 2) ? "\n" : "  ");
  printf("rip = 0x%016llx  rflags = 0x%016llx  cr0=0x%llx cr3=0x%llx\n",
         (unsigned long long)xemu.rip, (unsigned long long)xemu.rflags,
         (unsigned long long)xemu.cr0, (unsigned long long)xemu.cr3);
}

static void monitor(void) {
  char line[256];
  printf("XEMU monitor. Type 'help' for commands.\n");
  while (!xemu.halted) {
    printf("(xemu) ");
    fflush(stdout);
    if (!fgets(line, sizeof(line), stdin)) break;
    char *p = line;
    while (isspace((unsigned char)*p)) p++;
    if (!strncmp(p, "help", 4)) {
      printf("si [N]  step N instructions\ninfo r  show registers\n"
             "x N ADDR  examine N bytes\nq       quit\n");
    } else if (!strncmp(p, "si", 2)) {
      int n = 1;
      if (p[2]) n = atoi(p + 2);
      for (int i = 0; i < n && !xemu.halted; i++)
        if (step_once() != 0) break;
      dump_regs();
    } else if (!strncmp(p, "info", 4)) {
      dump_regs();
    } else if (p[0] == 'x') {
      unsigned n = 16; unsigned long long addr = xemu.rip;
      sscanf(p + 1, "%u %llx", &n, &addr);
      for (unsigned i = 0; i < n; i += 16) {
        printf("0x%016llx:", addr + i);
        for (unsigned j = 0; j < 16 && i + j < n; j++) {
          uint8_t v = 0; xemu_mem_read(addr + i + j, &v, 1);
          printf(" %02x", v);
        }
        printf("\n");
      }
    } else if (p[0] == 'q') {
      return;
    }
  }
}

static void dump_mem_and_exit(void) {
  if (!dump_mem) return;
  unsigned long long addr = 0, len = 0;
  if (sscanf(dump_mem, "%llx:%llx", &addr, &len) != 2) return;
  for (unsigned long long off = 0; off < len; off += 16) {
    printf("%016llx ", addr + off);
    for (unsigned long long k = 0; k < 16 && off + k < len; k++) {
      uint8_t b = (addr + off + k < XEMU_MEM_SIZE) ? xemu_mem[addr + off + k] : 0;
      printf("%02x ", b);
    }
    printf("\n");
  }
}

static void usage(const char *prog) {
  fprintf(stderr, "usage: %s -i IMAGE [-b] [--max-steps=N]\n", prog);
  fprintf(stderr, "  -i IMAGE       raw binary loaded at 0x400000\n");
  fprintf(stderr, "  -b             batch mode (run until halt)\n");
  fprintf(stderr, "  --max-steps=N  instruction limit (default 100000000)\n");
  fprintf(stderr, "  --trace        print an instruction trace (NEMU itrace style)\n");
  fprintf(stderr, "  --dump-mem=ADDR:LEN  hex-dump guest memory and exit\n");
  fprintf(stderr, "  --kernel FILE  boot a Linux bzImage via the 64-bit boot protocol\n");
  fprintf(stderr, "  --cmdline=STR  kernel command line (default: console=ttyS0)\n");
  fprintf(stderr, "  --mem=N        informational (memory size is build-time)\n");
}

int main(int argc, char **argv) {
  const char *image = NULL;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-b")) batch = 1;
    else if (!strcmp(argv[i], "-i") && i + 1 < argc) image = argv[++i];
    else if (!strncmp(argv[i], "--image=", 8)) image = argv[i] + 8;
    else if (!strncmp(argv[i], "--max-steps=", 12)) max_steps = atoll(argv[i] + 12);
    else if (!strcmp(argv[i], "--trace")) trace = 1;
    else if (!strcmp(argv[i], "--trace-cr")) trace_cr = 1;
    else if (!strncmp(argv[i], "--dump-mem=", 11)) dump_mem = argv[i] + 11;
    else if (!strcmp(argv[i], "--kernel") && i + 1 < argc) kernel_path = argv[++i];
    else if (!strncmp(argv[i], "--cmdline=", 10)) cmdline = argv[i] + 10;
    else if (!strncmp(argv[i], "--mem=", 6)) mem_mb = (unsigned)atoi(argv[i] + 6);
    else { usage(argv[0]); return 2; }
  }
  if (!image && !kernel_path) { usage(argv[0]); return 2; }
  xemu_reset();
  if (kernel_path) {
    if (xemu_boot_kernel(kernel_path, cmdline) != 0) return 2;
    xemu.rflags = 0x202;
    xemu.cs = 0x8;
    xemu.ss = 0x10;
  } else {
    if (load_image(image) != 0) return 2;
    xemu.rip = ENTRY;
    xemu.rflags = 0x202;
    xemu.cs = 0x8;
    xemu.ss = 0x10;
    printf("xemu: loaded %s at 0x%llx (%llu MiB RAM)\n", image,
           (unsigned long long)ENTRY,
           (unsigned long long)(XEMU_MEM_SIZE >> 20));
  }
  (void)mem_mb;

  if (batch) {
    while (!xemu.halted && (long long)steps < max_steps) {
      if (step_once() != 0) { dump_mem_and_exit(); dump_regs(); return 1; }
    }
  } else {
    monitor();
  }
  dump_mem_and_exit();
  printf("xemu: %s after %lu instructions, rip=0x%llx, exit=%llu\n",
         xemu.halted ? "halted" : "stopped", steps,
         (unsigned long long)xemu.rip, (unsigned long long)(xemu_exit_code & 0xff));
  dump_regs();
  return xemu.halted ? (int)(xemu_exit_code & 0xff) : 0;
}
