#include "xemu.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int xemu_mem_read(uint64_t addr, void *buf, size_t n) {
  if (addr + n <= XEMU_MEM_SIZE) {
    memcpy(buf, &xemu_mem[addr], n);
    return 0;
  }
  if (addr >= XEMU_MMIO_TIMER && addr + n <= XEMU_MMIO_TIMER + 0x1000) {
    uint32_t off = (uint32_t)(addr - XEMU_MMIO_TIMER);
    uint64_t v = 0;
    switch (off) {
      case 0: v = xemu.tsc & 0xffffffffu; break;
      case 4: v = xemu.tsc >> 32; break;
      case 8: v = xemu.timer_period; break;
      case 12: v = (uint64_t)xemu.irq_enabled; break;
      case 16: v = (uint64_t)xemu.irq_pending; break;
      default: v = 0; break;
    }
    memset(buf, 0, n);
    memcpy(buf, &v, n < 8 ? n : 8);
    return 0;
  }
  if (addr >= XEMU_MMIO_HALT && addr + n <= XEMU_MMIO_HALT + XEMU_MMIO_SIZE) {
    memset(buf, 0, n);
    return 0;
  }
  return -1;
}

uint64_t xemu_watch_lo = 0, xemu_watch_hi = 0, xemu_watch_pc = 0, xemu_watch_val = 0;
int xemu_watch_hit = 0, xemu_watch_en = 0, xemu_watch_byte = -1;
int xemu_watch_skip = 0;
uint64_t xemu_watch_steps = 0, xemu_watch_min_steps = 0;

int xemu_mem_write(uint64_t addr, const void *buf, size_t n) {
  if (xemu_watch_en && xemu_watch_lo && !xemu_watch_hit &&
      addr < xemu_watch_hi && addr + n > xemu_watch_lo) {
    /* Report the first byte that lands inside the watched window. */
    uint64_t off = xemu_watch_lo > addr ? xemu_watch_lo - addr : 0;
    uint64_t v = 0;
    const uint8_t *b = (const uint8_t *)buf;
    for (size_t i = 0; i < n && i < 8; i++) v |= (uint64_t)b[i] << (8 * i);
    int match = 1;
    if (xemu_watch_byte >= 0) {
      /* Require every byte of the store to equal the filter: a memset/rep
         stos chunk, not a pointer that merely contains the byte. */
      for (size_t i = 0; i < n; i++)
        if (b[i] != (uint8_t)xemu_watch_byte) { match = 0; break; }
    }
    if (match && xemu_watch_steps >= xemu_watch_min_steps) {
      if (xemu_watch_skip > 0) xemu_watch_skip--;
      else {
      xemu_watch_hit = 1;
      xemu_watch_pc = xemu.rip;
      (void)off;
      xemu_watch_val = v;
      }
    }
  }
  if (addr + n <= XEMU_MEM_SIZE) {
    memcpy(&xemu_mem[addr], buf, n);
    return 0;
  }
  if (addr >= XEMU_MMIO_TIMER && addr + n <= XEMU_MMIO_TIMER + 0x1000) {
    uint32_t off = (uint32_t)(addr - XEMU_MMIO_TIMER);
    uint64_t v = 0;
    memcpy(&v, buf, n < 8 ? n : 8);
    switch (off) {
      case 8: xemu.timer_period = v; xemu.timer_counter = v; break;
      case 12: xemu.irq_enabled = (int)(v & 1); break;
      case 20: xemu.irq_pending = (int)(v & 0xff); break;
      default: break;
    }
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

/* ---------------------------------------------------------------- TLB
 *
 * The kernel zeroes early_top_pgt while still running on it, relying on the
 * TLB to keep the old translations alive. Without a TLB every access would
 * fault immediately. A small direct-mapped cache reproduces that behaviour.
 * Entries are flushed on CR3 writes and INVLPG.
 *
 * Besides the VA->PA mapping an entry caches the leaf permissions (U/S,
 * R/W and NX), so the privilege checks apply to TLB hits as well as to page
 * walks. A/D are sticky: a translation records the accessed bit when it is
 * established and the dirty bit on its first successful write; later hits do
 * not rewrite the page tables again.
 */
#define TLB_BITS 10
#define TLB_SIZE (1u << TLB_BITS)

#define CR0_WP   (1ull << 16)
#define CR0_PG   (1ull << 31)
#define EFER_NXE (1ull << 11)

typedef struct {
  uint64_t va;       /* page-aligned virtual address, 0 = empty */
  uint64_t pa;       /* page-aligned physical address */
  uint64_t pte_pa;   /* physical address of the leaf page-table entry */
  uint8_t  present;
  uint8_t  writable; /* leaf R/W */
  uint8_t  user;     /* effective U/S (AND over all levels) */
  uint8_t  exec;     /* leaf executable under the current EFER.NXE */
  uint8_t  dirty;    /* D already recorded for this translation */
  uint8_t  level;    /* leaf level: 1 = 1 GiB, 2 = 2 MiB, 3 = 4 KiB */
} TlbEnt;

static TlbEnt tlb[TLB_SIZE];
uint64_t tlb_hits, tlb_misses;
static uint64_t tlb_cr3 = ~0ull;

void xemu_tlb_flush(void) {
  memset(tlb, 0, sizeof(tlb));
}

static void tlb_check_cr3(void) {
  if (tlb_cr3 != xemu.cr3) { xemu_tlb_flush(); tlb_cr3 = xemu.cr3; }
}

void xemu_tlb_invlpg(uint64_t va) {
  va &= ~0xfffull;
  for (unsigned i = 0; i < TLB_SIZE; i++)
    if (tlb[i].present && tlb[i].va == va) tlb[i].present = 0;
}

/* Cache a successful translation together with its leaf permissions. */
static void tlb_fill(uint64_t va, uint64_t pa, uint64_t pte_pa, int level,
                     int writable, int user, int exec, int dirty) {
  unsigned i = (unsigned)((va >> 12) & (TLB_SIZE - 1));
  TlbEnt *e = &tlb[i];
  e->va = va & ~0xfffull;
  e->pa = pa & ~0xfffull;
  e->pte_pa = pte_pa;
  e->level = (uint8_t)level;
  e->present = 1;
  e->writable = (uint8_t)(writable != 0);
  e->user = (uint8_t)(user != 0);
  e->exec = (uint8_t)(exec != 0);
  e->dirty = (uint8_t)(dirty != 0);
}

/* Does a cached translation still satisfy this access? */
static int tlb_allows(const TlbEnt *e, int write, int fetch) {
  if (xemu.cpl == 3 && !e->user) return 0;
  /* A supervisor write may bypass R/W only while CR0.WP is clear. */
  if (write && !e->writable && (xemu.cpl == 3 || (xemu.cr0 & CR0_WP)))
    return 0;
  if (fetch && !e->exec) return 0;
  return 1;
}

/* Record the dirty bit for a cached translation.  The page-table entry may
 * have been reused since the walk (Linux zeroes early_top_pgt while it is
 * still active), so only touch it while it describes the same page. */
static void tlb_set_dirty(TlbEnt *e) {
  if (e->dirty) return;
  e->dirty = 1;
  uint64_t ent;
  if (xemu_mem_read(e->pte_pa, &ent, 8) != 0) return;
  uint64_t mask;
  switch (e->level) {
    case 1:  mask = 0x000fffffc0000000ull; break;
    case 2:  mask = 0x000fffffffe00000ull; break;
    default: mask = 0x000ffffffffff000ull; break;
  }
  if (!(ent & 1) || (ent & mask) != (e->pa & mask)) return;
  ent |= (1ull << 5) | (1ull << 6);   /* A | D */
  (void)xemu_mem_write(e->pte_pa, &ent, 8);
}

/* ---------------------------------------------------------------- paging */
/* #PF error code bits (Intel SDM Table 4-21). */
#define PF_P    1ull   /* 0 = not present, 1 = protection violation */
#define PF_W    2ull   /* the access was a write */
#define PF_U    4ull   /* the access originated at CPL3 */
#define PF_RSVD 8ull   /* reserved bit violation */
#define PF_ID   16ull  /* the access was an instruction fetch */

/* With 4-level paging and CR4.PKE == 0 the bits 62:52 of a paging-structure
 * entry are ignored (software-available): Linux 6.6 stores _PAGE_DEVMAP in
 * bit 57 and _PAGE_SAVED_DIRTY in bit 58, and treating those as reserved made
 * every write-protected user PTE (fork/COW) die in page_fault_oops with
 * "Corrupted page table".  Only the physical-address bits above MAXPHYADDR
 * are genuinely reserved; XEMU reports MAXPHYADDR = 36 via CPUID.80000008h. */
#define XEMU_MAXPHYADDR 36
#define PTE_RSVD_MASK (((1ull << 52) - 1) & ~((1ull << XEMU_MAXPHYADDR) - 1))

static int page_fault(uint64_t va, uint64_t err, uint64_t *errcode) {
  xemu.cr2 = va;
  *errcode = err;
  return -1;
}

int xemu_translate(uint64_t va, int write, int fetch, uint64_t *pa, uint64_t *errcode) {
  const uint64_t base_err = (write ? PF_W : 0) | (xemu.cpl == 3 ? PF_U : 0) |
                            (fetch ? PF_ID : 0);
  if (!(xemu.cr0 & CR0_PG)) { *pa = va; *errcode = 0; return 0; }

  tlb_check_cr3();

  unsigned ti = (unsigned)((va >> 12) & (TLB_SIZE - 1));
  TlbEnt *te = &tlb[ti];
  int tlb_hit = te->present && te->va == (va & ~0xfffull);
  if (tlb_hit && tlb_allows(te, write, fetch)) {
    if (write) tlb_set_dirty(te);
    tlb_hits++;
    *pa = te->pa | (va & 0xfffull);
    *errcode = 0;
    return 0;
  }
  tlb_misses++;

  static int dbg = -1;
  if (dbg < 0) dbg = getenv("XEMU_WALK") ? 1 : 0;
  if (dbg && (va >> 12) == 0xC00)
    fprintf(stderr, "[walk] va=0x%llx w=%d cr3=0x%llx\n",
            (unsigned long long)va, write, (unsigned long long)xemu.cr3);
  uint64_t table = xemu.cr3 & ~0xfffull;
  int user_ok = 1;
  for (int level = 0; level < 4; level++) {
    uint64_t idx = (va >> (12 + 9 * (3 - level))) & 0x1ff;
    uint64_t epa = table + idx * 8;
    uint64_t e;
    if (xemu_mem_read(epa, &e, 8) != 0)
      return page_fault(va, base_err, errcode);
    /* A not-present entry has no reserved-bit semantics; software is free to
     * use the other bits (Linux stores swap/PROT_NONE information there). */
    if (!(e & 1))
      return page_fault(va, base_err, errcode);       /* P = 0 */
    if (e & PTE_RSVD_MASK)
      return page_fault(va, base_err | PF_P | PF_RSVD, errcode);
    if (!(e & 4)) user_ok = 0;

    int is_leaf = (level == 3) ||
                  ((level == 1 || level == 2) && (e & 0x80));
    /* Bit 63 is reserved in a non-leaf entry. */
    if (!is_leaf && (e & (1ull << 63)))
      return page_fault(va, base_err | PF_P | PF_RSVD, errcode);
    if (!is_leaf) {
      /* The R/W flag of the upper levels is enforced too: Linux documents
       * that page tables must be writable for their children to be. */
      if (write && !(e & 2))
        return page_fault(va, base_err | PF_P, errcode);
      table = e & 0x000ffffffffff000ull;
      continue;
    }

    uint64_t pa_mask, off_mask;
    switch (level) {
      case 1:  pa_mask = 0x000fffffc0000000ull; off_mask = 0x3fffffffull; break;
      case 2:  pa_mask = 0x000fffffffe00000ull; off_mask = 0x1fffffull; break;
      default: pa_mask = 0x000ffffffffff000ull; off_mask = 0xfffull; break;
    }
    uint64_t phys = (e & pa_mask) + (va & off_mask);

    /* U/S: CPL3 may only touch pages that are user-accessible at every
     * level.  The page is present, so P = 1 in the error code. */
    if (xemu.cpl == 3 && !user_ok)
      return page_fault(va, base_err | PF_P, errcode);
    /* R/W: a value of 0 means read-only.  CPL3 writes always fault; CPL0
     * writes fault only when CR0.WP is set. */
    if (write && !(e & 2) && (xemu.cpl == 3 || (xemu.cr0 & CR0_WP)))
      return page_fault(va, base_err | PF_P, errcode);
    int exec = !((e & (1ull << 63)) && (xemu.efer & EFER_NXE));
    if (fetch && !exec)
      return page_fault(va, base_err | PF_P, errcode);

    /* Set A on any successful access and D on a successful write. */
    uint64_t ne = e | (1ull << 5) | (write ? (1ull << 6) : 0);
    if (ne != e && xemu_mem_write(epa, &ne, 8) == 0) e = ne;

    tlb_fill(va, phys, epa, level, (e & 2) != 0, user_ok, exec, write);
    *pa = phys;
    *errcode = 0;
    return 0;
  }
  return page_fault(va, base_err, errcode);
}

uint64_t xemu_fault_err = 0;
uint64_t xemu_cr2_at_fault = 0;
uint64_t xemu_fault_pc = 0;
uint64_t xemu_exit_code = 0;
int xemu_fault_pending = 0;

static int vaccess(uint64_t va, void *buf, size_t n, int write, int fetch) {
  uint8_t *p = (uint8_t *)buf;
  while (n) {
    uint64_t pa, err;
    if (xemu_translate(va, write, fetch, &pa, &err) != 0) {
      /* CR2 holds the faulting linear address for every #PF, read included.
       * The kernel's early boot #PF handler reads CR2 to build the missing
       * direct-map entry on demand, so a stale value breaks boot. */
      xemu.cr2 = va;
      xemu_fault_err = err;
      xemu_cr2_at_fault = va;
      xemu_fault_pc = xemu.rip;
      return -1;
    }
    size_t chunk = 0x1000 - (va & 0xfff);
    if (chunk > n) chunk = n;
    if (write ? xemu_mem_write(pa, p, chunk) : xemu_mem_read(pa, p, chunk)) {
      if (getenv("XEMU_DBG_VA"))
        fprintf(stderr, "[va] %s va=0x%llx pa=0x%llx n=%zu rip=0x%llx\n",
                write ? "write" : "read", (unsigned long long)va,
                (unsigned long long)pa, n, (unsigned long long)xemu.rip);
      xemu.cr2 = va;
      xemu_fault_err = err;
      return -1;
    }
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

uint64_t xemu_read_tss_rsp0(void) {
  uint64_t v = 0;
  if (xemu.tr_base == 0) {
    fprintf(stderr, "[xemu] TSS base is zero (LTR not executed?)\n");
    return 0;
  }
  if (xemu_vread(xemu.tr_base + 4, &v, 8) != 0) {
    fprintf(stderr, "[xemu] TSS RSP0 read failed at 0x%llx\n",
            (unsigned long long)(xemu.tr_base + 4));
    return 0;
  }
  /* rsp0 is a full 64-bit field in the 64-bit TSS; masking it to 32 bits
     turns e.g. 0xffff888000002fc8 into 0x2fc8 and the first user->kernel
     transition pushes the trap frame at an unmapped address. */
  return v;
}

int xemu_gdt_read(uint64_t sel, uint64_t *base, uint64_t *limit, uint32_t *access) {
  uint64_t idx = (sel >> 3) & 0x1fff;
  uint64_t addr = xemu.gdtr_base + idx * 8;
  /* A 64-bit descriptor is 16 bytes: bytes 8..11 hold base[63:32] (system
     descriptors such as the TSS use it; the TSS lives in the CPU-entry area
     at 0xfffffe0000..., so dropping it yields a bogus low base). */
  uint8_t d[16];
  /* Reading the GDT is a supervisor access even when the instruction that
     triggered it runs at CPL3. */
  int saved_cpl = xemu.cpl;
  xemu.cpl = 0;
  int rc = xemu_vread(addr, d, 16);
  xemu.cpl = saved_cpl;
  if (rc != 0) return -1;
  uint32_t lo = (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
  uint32_t hi = (uint32_t)d[4] | ((uint32_t)d[5] << 8) | ((uint32_t)d[6] << 16) | ((uint32_t)d[7] << 24);
  uint64_t b = (lo >> 16) & 0xffff;
  b |= (uint64_t)(hi & 0xff) << 16;
  b |= (uint64_t)((hi >> 24) & 0xff) << 24;
  b |= ((uint64_t)d[8] | ((uint64_t)d[9] << 8) | ((uint64_t)d[10] << 16) |
        ((uint64_t)d[11] << 24)) << 32;
  if (access) *access = (hi >> 8) & 0xff;
  if (limit) {
    uint64_t l = lo & 0xffff;
    l |= (uint64_t)((hi >> 16) & 0xf) << 16;
    if (hi & (1u << 23)) l = (l << 12) | 0xfff;
    *limit = l;
  }
  if (base) *base = b;
  return 0;
}

uint64_t xemu_exc_count[32];
uint64_t xemu_exc_pc[32];
uint64_t xemu_exc_err[32];
int xemu_exc_delivered = 0;
uint64_t xemu_exc_seq = 0;
int xemu_trace_exc = 0;
int xemu_trap_frame_compat = 0;

void xemu_raise_exc(uint64_t vec, uint64_t err, uint64_t pc) {
  int from_cpl = xemu.cpl;
  if (vec < 32) { xemu_exc_count[vec]++; xemu_exc_pc[vec] = pc; xemu_exc_err[vec] = err; }
  xemu_exc_seq++;
  if (xemu_trace_exc)
    fprintf(stderr, "[exc] #%llu vec=%llu err=0x%llx pc=0x%llx cr2=0x%llx cpl=%d cs=0x%llx ss=0x%llx rsi=0x%llx rdi=0x%llx rax=0x%llx\n",
            (unsigned long long)xemu_exc_seq, (unsigned long long)vec,
            (unsigned long long)err, (unsigned long long)pc,
            (unsigned long long)xemu.cr2, from_cpl,
            (unsigned long long)xemu.cs, (unsigned long long)xemu.ss,
            (unsigned long long)xemu.gpr[6], (unsigned long long)xemu.gpr[7],
            (unsigned long long)xemu.gpr[0]);
  /* Exception delivery is a supervisor operation: the IDT/TSS reads and the
     kernel-stack frame pushes must not be rejected by the CPL3 U/S check.
     xemu.cpl is set to the handler's privilege at the end. */
  xemu.cpl = 0;
  uint8_t gate[16];
  uint64_t base = xemu.idtr_base + vec * 16;
  /* The IDT is addressed virtually when paging is on. Fall back to a physical
   * read so test images that never build a mapping for the IDT still work. */
  if (xemu_vread(base, gate, 16) != 0 && xemu_mem_read(base, gate, 16) != 0) {
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
  uint32_t attr = (uint32_t)((low >> 40) & 0xff); /* IDT type_attr byte */
  int gate_type = (int)(attr & 0xf);              /* 0xE interrupt, 0xF trap */
  int dpl = (int)((attr >> 5) & 3);
  int is_interrupt_gate = (gate_type == 0xe);
  int to_cpl = (int)(sel & 3);

  /* DPL check for software interrupts (vectors < 32 bypass this). */
  if (vec < 32 && 0) { /* architecture checks INT n; handled by caller */ }
  if (vec >= 32 && dpl < from_cpl) {
    /* should not happen for IRQs; fall through */
  }

  uint64_t old_rsp = xemu.gpr[4];
  uint64_t old_flags = xemu.rflags;
  uint64_t old_cs = xemu.cs;
  uint64_t old_ss = xemu.ss;

  /* The RTL trap microcode always pushes SS:RSP; mirror that so difftest
     lockstep matches for same-level traps too. */
  uint64_t new_rsp = old_rsp;
  if (from_cpl != to_cpl) {
    uint64_t rsp0 = xemu_read_tss_rsp0();
    if (rsp0 == 0) {
      fprintf(stderr, "[xemu] trap to CPL0 without a valid TSS RSP0 (vec=%llu)\n",
              (unsigned long long)vec);
      xemu.halted = 1;
      return;
    }
    new_rsp = rsp0;
  }

  /* Architecturally the frame carries the interrupted CS/SS. Linux checks
     the frame's CS on the way out of an exception: a same-level #PF frame
     with CS=8 is __KERNEL32_CS, so the kernel takes the 32-bit
     `native_irq_return_ldt` path and dies. Push the real selectors. */
  uint64_t frame_ss = xemu_trap_frame_compat ? 0 : old_ss;
  uint64_t frame_cs = (xemu_trap_frame_compat && from_cpl == to_cpl && to_cpl == 0)
                          ? 8 : old_cs;
  uint64_t sp = new_rsp;
  sp -= 8; xemu_vwrite(sp, &frame_ss, 8);
  sp -= 8; xemu_vwrite(sp, &old_rsp, 8);
  sp -= 8; xemu_vwrite(sp, &old_flags, 8);
  sp -= 8; xemu_vwrite(sp, &frame_cs, 8);
  sp -= 8; xemu_vwrite(sp, &pc, 8);
  if (trap_has_err(vec)) {
    sp -= 8; xemu_vwrite(sp, &err, 8);
  }
  xemu.gpr[4] = sp;
  xemu.cs = sel;
  if (from_cpl != to_cpl) xemu.ss = (uint64_t)((sel + 8));
  xemu.cpl = to_cpl;
  if (is_interrupt_gate) xemu.rflags = old_flags & ~(1ull << 9);
  xemu.rip = handler;
  /* Tell the instruction loop that RIP already points at the handler;
   * it must not add the instruction length on top. */
  xemu_exc_delivered = 1;
}

int xemu_request_irq(uint64_t vec) {
  if (vec < 32 || vec > 255) return -1;
  if (!(xemu.rflags & (1ull << 9))) return 0;   /* IF=0: stay pending */
  if (xemu.irq_pending) return 0;
  xemu_raise_exc(vec, 0, xemu.rip);
  return 1;
}

void xemu_timer_tick(void) {
  xemu.tsc++;
  if (!xemu.irq_enabled || xemu.timer_period == 0) return;
  if (xemu.timer_counter == 0) return;
  if (--xemu.timer_counter == 0) {
    xemu.timer_counter = xemu.timer_period;
    xemu.irq_pending = 32;
  }
}
