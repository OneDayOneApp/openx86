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
static const char *initrd_path = NULL;
static unsigned mem_mb = 0;
static int batch = 0;
static int trace = 0;
static int pc_seen_add(uint64_t v);
static void dump_regs(void);

static int trace_cr = 0;
static const char *watch_pc = NULL;
static const char *find_str = NULL;
static const char *stop_at = NULL;
static FILE *trace_pc_fp = NULL;      /* --trace-pc=FILE: one PC per line */
static long long stop_at_exc = -1;    /* --stop-at-exc=N */
static const char *dump_vmem = NULL;  /* --dump-vmem=ADDR:LEN (virtual) */
static int verbose_exc = 0;           /* --trace-exc */
static int pc_unique = 0;
static int pc_ring = 0;
static uint64_t *pc_ring_buf;
static size_t pc_ring_pos;
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
  if (stop_at) {
    /* Checked before decoding: the stop address may itself be unfetchable
       (e.g. a corrupted branch target), and that is exactly when we want to
       stop and look at the recent PC history. */
    unsigned long long addr = 0;
    if (sscanf(stop_at, "%llx", &addr) == 1 && xemu.rip == addr) {
      fprintf(stderr, "stop-at: reached 0x%llx after %lu instructions\n"
              "  rax=0x%llx rcx=0x%llx rdx=0x%llx rsi=0x%llx rdi=0x%llx rbx=0x%llx idtr=0x%llx cr2=0x%llx\n",
              (unsigned long long)addr, steps,
              (unsigned long long)xemu.gpr[0], (unsigned long long)xemu.gpr[1],
              (unsigned long long)xemu.gpr[2], (unsigned long long)xemu.gpr[6],
              (unsigned long long)xemu.gpr[7], (unsigned long long)xemu.gpr[3],
              (unsigned long long)xemu.idtr_base, (unsigned long long)xemu.cr2);
      dump_regs();
      return 1;
    }
  }
  if (trace_pc_fp) fprintf(trace_pc_fp, "0x%llx\n", (unsigned long long)xemu.rip);
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
  if (watch_pc) {
    unsigned long long lo = 0, hi = 0;
    if (sscanf(watch_pc, "%llx-%llx", &lo, &hi) == 2 &&
        xemu.rip >= lo && xemu.rip < hi)
      fprintf(stderr, "pc in [%llx,%llx): 0x%llx (first hit)\n", lo, hi,
              (unsigned long long)xemu.rip);
  }
  if (pc_ring > 0) {
    /* Keep the last `pc_ring` executed PCs; dump on exit. */
    if (!pc_ring_buf) pc_ring_buf = calloc((size_t)pc_ring, sizeof(uint64_t));
    if (pc_ring_buf) {
      pc_ring_buf[pc_ring_pos % (size_t)pc_ring] = xemu.rip;
      pc_ring_pos++;
    }
  }
  if (pc_unique) {
    /* Record the first visit to each function granule (64 bytes). */
    uint64_t gran = xemu.rip & ~0x3full;
    if (pc_seen_add(gran))            /* 1 = newly inserted */
      fprintf(stderr, "PC 0x%llx\n", (unsigned long long)xemu.rip);
  }
  ring_add(xemu.rip, inst.op, inst.len);
  uint64_t prev_cs = xemu.cs, prev_ss = xemu.ss, prev_rip = xemu.rip;
  rc = xemu_exec(&inst);
  static int sel_logs = 0;
  if (verbose_exc && sel_logs < 40 &&
      (xemu.cs != prev_cs || xemu.ss != prev_ss))
    fprintf(stderr, "[sel] op=%u at 0x%llx cs 0x%llx->0x%llx ss 0x%llx->0x%llx rip 0x%llx->0x%llx\n",
            (unsigned)inst.op, (unsigned long long)prev_rip,
            (unsigned long long)prev_cs, (unsigned long long)xemu.cs,
            (unsigned long long)prev_ss, (unsigned long long)xemu.ss,
            (unsigned long long)prev_rip, (unsigned long long)xemu.rip);
  if (verbose_exc && (xemu.cs != prev_cs || xemu.ss != prev_ss)) sel_logs++;
  if (rc != 0) {
    fprintf(stderr, "xemu: exec failed at rip=0x%llx op=%u\n",
            (unsigned long long)xemu.rip, (unsigned)inst.op);
    ring_dump();
    return -1;
  }
  steps++;
  return 0;
}

static void dump_pc_ring(void) {
  size_t n = pc_ring_pos < (size_t)pc_ring ? pc_ring_pos : (size_t)pc_ring;
  fprintf(stderr, "last %zu executed PCs:\n", n);
  for (size_t i = 0; i < n; i++) {
    size_t idx = (pc_ring_pos - n + i) % (size_t)pc_ring;
    fprintf(stderr, "  #%zu 0x%llx\n", i, (unsigned long long)pc_ring_buf[idx]);
  }
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

/* Scan guest RAM for a byte string; used to prove printk ran (the kernel's
 * log buffer lives in .bss and is not at a fixed address without symbols). */
/* Small open-addressing set of visited 64-byte PC granules. */
static uint64_t *pc_seen_tab;
static size_t pc_seen_cap;

static int pc_seen_add(uint64_t v) {
  if (!pc_seen_tab) {
    pc_seen_cap = 1u << 22;                 /* 4M slots */
    pc_seen_tab = calloc(pc_seen_cap, sizeof(uint64_t));
    if (!pc_seen_tab) return 0;
  }
  size_t i = (size_t)((v * 0x9E3779B97F4A7C15ull) >> 40) & (pc_seen_cap - 1);
  for (size_t probes = 0; probes < 64; probes++) {
    if (pc_seen_tab[i] == 0) { pc_seen_tab[i] = v; return 1; }
    if (pc_seen_tab[i] == v) return 0;
    i = (i + 1) & (pc_seen_cap - 1);
  }
  return 0;
}

static void find_in_memory(const char *needle) {
  size_t n = strlen(needle);
  int hits = 0;
  for (uint64_t a = 0; a + n <= XEMU_MEM_SIZE; a++) {
    if (memcmp(&xemu_mem[a], needle, n) == 0) {
      printf("found \"%s\" at 0x%llx\n", needle, (unsigned long long)a);
      if (++hits >= 8) break;
    }
  }
  if (!hits) printf("\"%s\" not found in RAM\n", needle);
}

/* Dump guest memory through the MMU: works for kernel virtual addresses. */
static void dump_vmem_and_exit(void) {
  unsigned long long addr = 0, len = 0;
  if (sscanf(dump_vmem, "%llx:%llx", &addr, &len) != 2) return;
  for (unsigned long long off = 0; off < len; off += 16) {
    printf("%016llx ", addr + off);
    for (unsigned long long k = 0; k < 16 && off + k < len; k++) {
      uint8_t b = 0;
      if (xemu_vread(addr + off + k, &b, 1) != 0) { printf(" ?? "); continue; }
      printf(" %02x ", b);
    }
    printf(" |");
    for (unsigned long long k = 0; k < 16 && off + k < len; k++) {
      uint8_t b = 0;
      if (xemu_vread(addr + off + k, &b, 1) != 0) { putchar('?'); continue; }
      putchar((b >= 0x20 && b < 0x7f) ? (int)b : '.');
    }
    printf("|\n");
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
  fprintf(stderr, "  --dump-mem=ADDR:LEN  hex-dump guest physical memory and exit\n");
  fprintf(stderr, "  --dump-vmem=ADDR:LEN hex-dump through the MMU (virtual)\n");
  fprintf(stderr, "  --trace-pc=FILE     write one executed PC per line\n");
  fprintf(stderr, "  --trace-exc         log every exception delivery\n");
  fprintf(stderr, "  --stop-at-exc=N     stop after the Nth exception delivery\n");
  fprintf(stderr, "  --watch-mem=LO:HI   stop at the first physical write into [LO,HI)\n");
  fprintf(stderr, "  --watch-byte=XX     only trigger if the written data contains byte XX\n");
  fprintf(stderr, "  --watch-va=ADDR     stop at the first write to a virtual address\n");
  fprintf(stderr, "  --serial-stop=STR   stop once STR appears on the serial console\n");
  fprintf(stderr, "  --kernel FILE  boot a Linux bzImage via the 64-bit boot protocol\n");
  fprintf(stderr, "  --cmdline=STR  kernel command line (default: console=ttyS0)\n");
  fprintf(stderr, "  --initrd FILE  initramfs image to hand to the kernel\n");
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
    else if (!strcmp(argv[i], "--trace-port")) xemu_trace_port = 1;
    else if (!strncmp(argv[i], "--watch-pc=", 11)) watch_pc = argv[i] + 11;
    else if (!strncmp(argv[i], "--find-str=", 11)) find_str = argv[i] + 11;
    else if (!strncmp(argv[i], "--stop-at=", 10)) stop_at = argv[i] + 10;
    else if (!strcmp(argv[i], "--pc-unique")) pc_unique = 1;
    else if (!strncmp(argv[i], "--pc-ring=", 10)) pc_ring = atoi(argv[i] + 10);
    else if (!strncmp(argv[i], "--dump-mem=", 11)) dump_mem = argv[i] + 11;
    else if (!strncmp(argv[i], "--dump-vmem=", 12)) dump_vmem = argv[i] + 12;
    else if (!strncmp(argv[i], "--trace-pc=", 11)) trace_pc_fp = fopen(argv[i] + 11, "w");
    else if (!strcmp(argv[i], "--trace-exc")) { verbose_exc = 1; xemu_trace_exc = 1; }
    else if (!strncmp(argv[i], "--stop-at-exc=", 14)) stop_at_exc = atoll(argv[i] + 14);
    else if (!strncmp(argv[i], "--watch-byte=", 13)) xemu_watch_byte = (int)strtol(argv[i] + 13, NULL, 16);
    else if (!strncmp(argv[i], "--watch-skip=", 13)) xemu_watch_skip = atoi(argv[i] + 13);
    else if (!strncmp(argv[i], "--watch-after=", 14)) xemu_watch_min_steps = strtoull(argv[i] + 14, NULL, 0);
    else if (!strncmp(argv[i], "--watch-va=", 11)) {
      xemu_watch_va = strtoull(argv[i] + 11, NULL, 16);
      if (!pc_ring) pc_ring = 32;
    }
    else if (!strncmp(argv[i], "--serial-stop=", 14)) xemu_serial_stop = argv[i] + 14;
    else if (!strncmp(argv[i], "--watch-mem=", 12)) {
      unsigned long long lo = 0, hi = 0;
      if (sscanf(argv[i] + 12, "%llx:%llx", &lo, &hi) == 2) {
        xemu_watch_lo = lo; xemu_watch_hi = hi;
        if (!pc_ring) pc_ring = 64;
      }
    }
    else if (!strcmp(argv[i], "--kernel") && i + 1 < argc) kernel_path = argv[++i];
    else if (!strncmp(argv[i], "--cmdline=", 10)) cmdline = argv[i] + 10;
    else if (!strcmp(argv[i], "--initrd") && i + 1 < argc) initrd_path = argv[++i];
    else if (!strncmp(argv[i], "--mem=", 6)) mem_mb = (unsigned)atoi(argv[i] + 6);
    else { usage(argv[0]); return 2; }
  }
  if (!image && !kernel_path) { usage(argv[0]); return 2; }
  xemu_reset();
  if (kernel_path) {
    if (xemu_boot_kernel_initrd(kernel_path, cmdline, initrd_path) != 0) return 2;
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

  xemu_watch_en = xemu_watch_lo != 0;
  xemu_watch_steps = 0;
  xemu_watch_steps = (uint64_t)steps;
  if (batch) {
    while (!xemu.halted && (long long)steps < max_steps) {
      if (step_once() != 0) {
        dump_mem_and_exit();
        dump_regs();
        if (pc_ring_buf && pc_ring_pos) dump_pc_ring();
        return 1;
      }
      xemu_watch_steps++;
      if (verbose_exc && xemu.rip == 0xffffffff819e23c0ull) {
        int32_t w = (int32_t)(uint32_t)xemu.gpr[1];
        w >>= 8;
        if (w > 0x100 || w < -0x100) {
          fprintf(stderr, "number(): huge field_width=%d spec=0x%llx buf=0x%llx end=0x%llx num=0x%llx\n",
                  w, (unsigned long long)xemu.gpr[1],
                  (unsigned long long)xemu.gpr[7], (unsigned long long)xemu.gpr[6],
                  (unsigned long long)xemu.gpr[2]);
          dump_regs();
          break;
        }
      }
      if (xemu_watch_va_hit) {
        fprintf(stderr, "watch-va: write to 0x%llx at rip=0x%llx value=0x%llx after %lu instructions\n",
                (unsigned long long)xemu_watch_va, (unsigned long long)xemu_watch_va_pc,
                (unsigned long long)xemu_watch_va_val, steps);
        dump_regs();
        if (pc_ring_buf && pc_ring_pos) dump_pc_ring();
        break;
      }
      if (xemu_watch_hit) {
        fprintf(stderr, "watch: write to [0x%llx,0x%llx) at rip=0x%llx value=0x%llx after %lu instructions\n",
                (unsigned long long)xemu_watch_lo, (unsigned long long)xemu_watch_hi,
                (unsigned long long)xemu_watch_pc, (unsigned long long)xemu_watch_val, steps);
        dump_regs();
        break;
      }
      if (stop_at_exc > 0 && (long long)xemu_exc_seq >= stop_at_exc) {
        fprintf(stderr, "stop-at-exc: %lld exceptions delivered after %lu instructions\n",
                (long long)xemu_exc_seq, steps);
        dump_regs();
        break;
      }
    }
  } else {
    monitor();
  }
  if (dump_vmem) dump_vmem_and_exit();
  if (trace_pc_fp) fclose(trace_pc_fp);
  if (find_str) find_in_memory(find_str);
  dump_mem_and_exit();
  if (pc_ring_buf && pc_ring_pos) dump_pc_ring();
  fprintf(stderr, "xemu: tlb hits=%llu misses=%llu\n",
          (unsigned long long)tlb_hits, (unsigned long long)tlb_misses);
  if (xemu_fault_pc)
    fprintf(stderr, "xemu: last translation fault at rip=0x%llx va=0x%llx err=0x%llx\n",
            (unsigned long long)xemu_fault_pc,
            (unsigned long long)xemu_cr2_at_fault,
            (unsigned long long)xemu_fault_err);
  for (int i = 0; i < 32; i++)
    if (xemu_exc_count[i])
      fprintf(stderr, "xemu: exception %d raised %llu times (pc=0x%llx err=0x%llx)\n", i,
              (unsigned long long)xemu_exc_count[i],
              (unsigned long long)xemu_exc_pc[i],
              (unsigned long long)xemu_exc_err[i]);
  if (verbose_exc)
    fprintf(stderr, "xemu: %llu exception deliveries in total\n",
            (unsigned long long)xemu_exc_seq);
  if (xemu_port_out_count || xemu_port_in_count)
    fprintf(stderr, "xemu: %llu port writes, %llu port reads\n",
            (unsigned long long)xemu_port_out_count,
            (unsigned long long)xemu_port_in_count);
  if (xemu_serial_stop && !xemu_serial_stopped)
    fprintf(stderr, "xemu: marker %s not seen; last serial bytes: %.200s\n",
            xemu_serial_stop, xemu_uart_serbuf());
  if (xemu_serial_stopped)
    fprintf(stderr, "xemu: stopped on serial marker %s\n", xemu_serial_stop);
  printf("xemu: %s after %lu instructions, rip=0x%llx, exit=%llu\n",
         xemu.halted ? "halted" : "stopped", steps,
         (unsigned long long)xemu.rip, (unsigned long long)(xemu_exit_code & 0xff));
  dump_regs();
  return xemu.halted ? (int)(xemu_exit_code & 0xff) : 0;
}
