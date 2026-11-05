#ifndef XEMU_H
#define XEMU_H

#include <stddef.h>
#include <stdint.h>

/* Physical memory size; overridable at build time (-DXEMU_MEM_SIZE_MB=256). */
#ifndef XEMU_MEM_SIZE_MB
#define XEMU_MEM_SIZE_MB 16
#endif
#define XEMU_MEM_SIZE ((uint64_t)XEMU_MEM_SIZE_MB * 1024 * 1024)
#define XEMU_MMIO_HALT 0xF0000000ull
#define XEMU_MMIO_TIMER 0xE0000000ull

/* Port I/O space. Devices register at fixed port ranges (L0). */
#define XEMU_PORT_UART 0x3F8
#define XEMU_PORT_UART2 0x2F8
#define XEMU_PORT_PIC_MASTER 0x20
#define XEMU_PORT_PIC_SLAVE 0xA0
#define XEMU_PORT_PIT 0x40
#define XEMU_PORT_KBD 0x60
#define XEMU_MMIO_SIZE 0x1000ull

/* Operation ids aligned with the RTL Ops numbering where practical. */
enum {
  XOP_NONE = 0,
  XOP_MOV = 1,
  XOP_HLT = 41,
};

/* Operand kinds mirror the RTL decoder: None/Gpr/Xmm/Mem/Imm. */
enum { XK_NONE = 0, XK_GPR = 1, XK_XMM = 2, XK_MEM = 3, XK_IMM = 4, XK_SEG = 5,
       XK_X87 = 6 };

typedef struct {
  uint64_t gpr[16];
  uint64_t rip, rflags;
  uint64_t xmm[16][2];
  uint64_t cr0, cr2, cr3, cr4;
  uint64_t idtr_base;
  uint16_t idtr_limit;
  uint64_t cs, ss, ds, es, fs, gs;
  uint64_t efer, star, lstar, sfmask;
  uint64_t sysenter_cs, sysenter_esp, sysenter_eip;
  uint64_t pat, mtrr_def_type;   /* IA32_PAT / IA32_MTRR_DEF_TYPE */
  uint64_t fs_base, gs_base, kernel_gs_base;
  uint32_t mxcsr;
  uint64_t dr[8];   /* debug registers */
  /* P3: segmentation/TSS/privilege/interrupt state. */
  uint64_t gdtr_base;
  uint16_t gdtr_limit;
  uint64_t tr_base;
  uint16_t tr_sel;
  uint64_t tr_limit;
  int cpl;
  int irq_pending;   /* external IRQ vector, 0 = none */
  int irq_enabled;
  uint64_t timer_period;
  uint64_t timer_counter;
  uint64_t tsc;
  int halted;
  int halted_cpu;   /* HLT executed; waiting for an interrupt */
  /* L0 devices */
  uint64_t iopl;
} XemuState;

typedef struct {
  uint8_t len;
  uint16_t op;
  uint8_t opsz; /* 1/2/4/8 */
  uint8_t simd;
  uint8_t memsz;
  uint8_t cond;
  uint8_t a_kind, b_kind;
  uint8_t a_idx, b_idx;
  uint64_t imm;
  uint8_t imm_kind;
  uint8_t dir; /* table direction bit: 1 = A is destination (see MOV_SEG) */
  uint8_t rep; /* 0 none, 1 F3, 2 F2 */
  uint8_t seg_ovr;  /* 0 none, 1 ES, 2 CS, 3 SS, 4 DS, 5 FS, 6 GS */
  uint8_t port_imm; /* IN/OUT with imm8 port */
  uint8_t io_sz;    /* 1/2/4 for IN/OUT/INS/OUTS */
  int64_t disp;
  uint8_t base, index, scale;
  int rip_rel;
  int has_base, has_index;
  int illegal;
} XemuInst;

extern XemuState xemu;
extern uint8_t xemu_mem[XEMU_MEM_SIZE];

void xemu_reset(void);

int xemu_mem_read(uint64_t addr, void *buf, size_t n);
int xemu_mem_write(uint64_t addr, const void *buf, size_t n);

/* Virtual-memory access (identity when paging is disabled). */
int xemu_vread(uint64_t va, void *buf, size_t n);
int xemu_vwrite(uint64_t va, const void *buf, size_t n);
int xemu_translate(uint64_t va, int write, int fetch, uint64_t *pa, uint64_t *errcode);
void xemu_tlb_flush(void);
extern uint64_t tlb_hits, tlb_misses;
void xemu_tlb_invlpg(uint64_t va);
void xemu_raise_exc(uint64_t vec, uint64_t err, uint64_t pc);
extern uint64_t xemu_exc_count[32];
extern uint64_t xemu_exc_pc[32];
extern uint64_t xemu_exc_err[32];
extern int xemu_exc_delivered;
/* Deliver an external interrupt (vector >= 32) if IF=1 and nothing is pending. */
int xemu_request_irq(uint64_t vec);
/* Advance the PIT-like timer; may set irq_pending. Called once per instruction. */
void xemu_timer_tick(void);
uint64_t xemu_read_tss_rsp0(void);
int xemu_gdt_read(uint64_t sel, uint64_t *base, uint64_t *limit, uint32_t *access);
void xemu_do_syscall(uint64_t next_rip);
void xemu_do_sysret(void);

/* Port I/O: returns 0 on success; unmapped ports read as all-ones. */
int xemu_port_in(uint32_t port, int size, uint64_t *val);
int xemu_port_out(uint32_t port, int size, uint64_t val);
/* Devices (L0). */
void xemu_uart_reset(void);
int xemu_uart_in(uint32_t off, int size, uint64_t *val);
int xemu_uart_out(uint32_t off, int size, uint64_t val);
void xemu_uart_poll(void);
void xemu_pic_reset(void);
int xemu_pic_in(uint32_t off, int size, uint64_t *val);
int xemu_pic_out(uint32_t off, int size, uint64_t val);
void xemu_pic_raise(int irq);
void xemu_pic_eoi_done(void);
void xemu_pit_reset(void);
int xemu_pit_in(uint32_t off, int size, uint64_t *val);
int xemu_pit_out(uint32_t off, int size, uint64_t val);
void xemu_pit_tick(void);
extern uint64_t xemu_pit_ips;
int xemu_pic_ack(void);
void xemu_after_inst(void);
/* x87 */
void xemu_fpu_reset(void);
void xemu_fpu_fxsave(uint64_t addr);
void xemu_fpu_fxrstor(uint64_t addr);
int xemu_fpu_exec(const XemuInst *d);
uint16_t xemu_fpu_cw(void);
uint16_t xemu_fpu_sw(void);
uint16_t xemu_fpu_tag(void);
/* Effective address of the ModRM memory operand (mem.c). */
uint64_t xemu_ea(const XemuInst *d);
extern int xemu_console_serial;
extern int xemu_trace_port;
extern uint64_t xemu_port_out_count;
extern uint64_t xemu_port_in_count;
extern uint64_t xemu_fault_err;
extern uint64_t xemu_cr2_at_fault;
extern uint64_t xemu_fault_pc;
extern uint64_t xemu_exit_code;
extern int xemu_fault_pending;
/* Diagnostics: every exception delivery bumps xemu_exc_seq and is logged to
 * stderr when xemu_trace_exc is set (see xemu_raise_exc in mem.c). */
extern uint64_t xemu_exc_seq;
extern int xemu_trace_exc;
/* Physical-memory watchpoint: stop at the first write overlapping
 * [xemu_watch_lo, xemu_watch_hi). Set by --watch-mem=LO:HI. */
extern uint64_t xemu_watch_lo, xemu_watch_hi, xemu_watch_pc, xemu_watch_val;
extern int xemu_watch_hit, xemu_watch_en, xemu_watch_byte;
extern int xemu_watch_skip;   /* ignore the first N matching writes */
/* Virtual-address watchpoint (--watch-va=ADDR): catches writes through the
 * MMU, which is what you need for a user stack address. */
extern uint64_t xemu_watch_va, xemu_watch_va_pc, xemu_watch_va_val;
extern int xemu_watch_va_hit;
/* The RTL subset core models no CS/SS state and hardcodes the same-level trap
 * frame selectors to SS=0/CS=8. difftest_init() turns this on so lockstep
 * compares against that model; the standalone runner keeps the real frame. */
extern int xemu_trap_frame_compat;
/* Boot-test helper: stop the run once this marker has been written to the
 * serial console (see --serial-stop=STR). */
extern const char *xemu_serial_stop;
extern int xemu_serial_stopped;
const char *xemu_uart_serbuf(void);
extern uint64_t xemu_watch_steps;  /* steps counter for --watch-after */
extern uint64_t xemu_watch_min_steps;

/* Returns 0 on success, negative on unsupported/invalid instruction. */
int xemu_decode(uint64_t rip, XemuInst *out);
/* Returns 0 on success, negative on fault. */
int xemu_exec(const XemuInst *inst);
/* Load a Linux bzImage and set up the entry state (see boot.c). */
int xemu_boot_kernel(const char *path, const char *cmdline);
int xemu_boot_kernel_initrd(const char *path, const char *cmdline, const char *initrd);

#endif
