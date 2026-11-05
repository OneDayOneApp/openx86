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
enum { XK_NONE = 0, XK_GPR = 1, XK_XMM = 2, XK_MEM = 3, XK_IMM = 4 };

typedef struct {
  uint64_t gpr[16];
  uint64_t rip, rflags;
  uint64_t xmm[16][2];
  uint64_t cr0, cr2, cr3, cr4;
  uint64_t idtr_base;
  uint16_t idtr_limit;
  uint64_t cs, ss;
  uint64_t efer, star, lstar, sfmask;
  uint64_t fs_base, gs_base;
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
  uint8_t rep; /* 0 none, 1 F3, 2 F2 */
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
void xemu_raise_exc(uint64_t vec, uint64_t err, uint64_t pc);
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
extern int xemu_console_serial;
extern uint64_t xemu_fault_err;
extern uint64_t xemu_exit_code;
extern int xemu_fault_pending;

/* Returns 0 on success, negative on unsupported/invalid instruction. */
int xemu_decode(uint64_t rip, XemuInst *out);
/* Returns 0 on success, negative on fault. */
int xemu_exec(const XemuInst *inst);

#endif
