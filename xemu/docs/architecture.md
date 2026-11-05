# XEMU 软件架构

XEMU 是 openx86 项目自研的 x86-64 参考模型（reference model）。
它有三个使用形态，但共享同一套核心：

| 形态 | 产物 | 用途 |
| --- | --- | --- |
| 差分校验库 | `build/ref/libxemu.so` | 被 RTL 仿真器 / Bochs 对比脚本 `dlopen`，作为参考模型 |
| 独立模拟器 | `build/xemu` | 跑裸机测试镜像、启动 Linux 内核（`--kernel`） |
| 锁步对端 | 同上两种 | 与 Verilator 生成的 RTL 逐指令对比（`sys-lockstep-test`） |

---

## 1. 分层视图

```
                    ┌─────────────────────────────────────────────┐
   宿主接口层        │  difftest.c        main.c                   │
   (L5)             │  NEMU 差分 ABI      CLI / monitor / 诊断开关  │
                    └───────┬────────────────────┬────────────────┘
                            │                    │
                    ┌───────▼────────────────────▼────────────────┐
   系统集成层        │  boot.c            异常/中断投递             │
   (L4)             │  Linux 引导协议     设备模型 (dev/)           │
                    │  bzImage / ELF      UART  PIC  PIT  port      │
                    └───────┬────────────────────┬────────────────┘
                            │                    │
                    ┌───────▼────────────────────┐│
   指令执行层        │  exec.c                    ││
   (L3)             │  分派 + ALU/flags/串/控制   ││
                    │  ├── sse.c    SSE/SSE2     ││
                    │  └── fpu.c    x87 + FXSAVE ││
                    └───────┬────────────────────┘│
                            │                     │
                    ┌───────▼────────────────────┐│
   取指解码层        │  decode.c  表驱动解码器     ││
   (L2)             │  ops.h     统一 op 编号     ││
                    └───────┬────────────────────┘│
                            │                     │
                    ┌───────▼─────────────────────▼┐
   存储地址层        │  mem.c                        │
   (L1)             │  物理内存 / MMIO / 4 级页表/TLB │
                    │  异常帧构造 / GDT·TSS 访问      │
                    └───────┬──────────────────────┘
                            │
                    ┌───────▼──────────┐
   全局状态          │  state.c         │
                    │  XemuState xemu  │  ← 所有层共享的单一全局状态
                    └──────────────────┘
```

依赖方向严格自上而下：上层只调用下层，`XemuState` 是唯一的全局锚点
（`xemu/xemu.h`），各层直接读写它，不做隐藏。

---

## 2. 模块清单

| 层 | 文件 | 行数 | 职责 |
| --- | --- | ---: | --- |
| L5 | `difftest.c` | 86 | 导出 NEMU/XiangShan 差分 ABI：`difftest_init/memcpy/regcpy/exec/skip_ref/raise_intr/halted` |
| L5 | `main.c` | 416 | 参数解析、批处理/监视器、诊断开关、退出时统计 |
| L4 | `boot.c` | 282 | Linux 引导协议：解析 bzImage / ELF、建零页与 e820、装 initrd、预置长模式恒等映射 |
| L4 | `dev/port.c` | 53 | 端口空间路由（未映射端口读全 1、写丢弃） |
| L4 | `dev/uart.c` | 156 | 16550 兼容串口，THR→stdout；stdin 轮询做成中断源 |
| L4 | `dev/pic.c` | 147 | 8259A 主/从，ICW1-4、OCW1-3、IRR/ISR/IMR、级联 |
| L4 | `dev/pit.c` | 124 | 8254 通道 0，模式 0/2/3，锁存读，计满触发 IRQ0 |
| L3 | `exec.c` | 1185 | 指令语义总入口、标志位、乘除移位、串操作、控制转移、段/特权、端口、MSR/CPUID |
| L3 | `sse.c` | 463 | SSE/SSE2（整数 SIMD、浮点、搬移、转换） |
| L3 | `fpu.c` | ~520 | x87 栈机、D8–DF 全寄存器形式、超越函数、`FCOMI` 族、`FXSAVE`/`FXRSTOR`、`MXCSR` |
| L2 | `decode.c` | 1146 | 表驱动解码：前缀、REX、ModRM/SIB、组扩展、三代 Opcode Map |
| L2 | `ops.h` | 188 | 统一 op 编号（与 RTL 对齐） |
| L1 | `mem.c` | 404 | 物理内存 + MMIO、4 级页表遍历 + TLB、异常投递与栈帧、GDT/TSS 读取 |
| — | `xemu.h` | 164 | `XemuState` / `XemuInst` / 常量 / 跨模块声明 |
| — | `state.c` | 20 | 复位到体系结构初始状态，并复位各设备 |

---

## 3. 核心数据结构

### 3.1 `XemuState` —— 单一全局状态

```
┌──────────────────────────── XemuState ────────────────────────────┐
│ 通用/指针   gpr[16]  rip  rflags                                   │
│ SIMD        xmm[16][2]  mxcsr                                     │
│ x87         由 fpu.c 内部静态持有（80 位扩展精度）                  │
│ 控制/调试   cr0 cr2 cr3 cr4   dr[8]                                │
│ 段与特权    cs ss ds es fs gs   cpl   gdtr/idtr/tr(base+limit)     │
│ 系统寄存器  efer star lstar sfmask  fs_base gs_base kernel_gs_base │
│ 中断        irq_pending  irq_enabled  iopl                         │
│ 定时        timer_period timer_counter tsc                         │
│ 运行控制    halted  halted_cpu                                     │
└───────────────────────────────────────────────────────────────────┘
```

设计取舍：**不做 CPU 对象抽象**。RTL 侧是单核，XEMU 也对齐单核，
一个全局结构体让 `difftest_regcpy` 与逐指令对比都变成一次 `memcpy`。

### 3.2 `XemuInst` —— 解码结果

解码器把「指令」压平成固定字段，执行器不再碰原始字节：

| 字段 | 说明 |
| --- | --- |
| `op` | 统一 op 编号（`ops.h`），执行器按它分派 |
| `len` / `opsz` | 指令长度 / 有效操作数宽度（1/2/4/8） |
| `a_kind`/`a_idx`,`b_kind`/`b_idx` | 两个操作数的种类与索引 |
| `imm` / `imm_kind` | 立即数及其编码类型（`IMM_8/Z/B/W`…） |
| `base`/`index`/`scale`/`disp`/`rip_rel` | 有效地址构成 |
| `seg_ovr` | 段覆盖前缀（FS/GS 参与有效地址计算） |
| `rep` / `cond` | 重复前缀 / 条件码或读写方向标志 |
| `port_imm` / `io_sz` | 端口 I/O 专用 |
| `simd` / `memsz` | SIMD 与内存操作数宽度 |

---

## 4. 单条指令的数据流

```
step_once()                                          main.c
   │
   ├─► xemu_decode(rip, &inst)                       decode.c
   │     ├─ xemu_translate(rip, fetch)   ──► #PF？ → xemu_raise_exc  mem.c
   │     ├─ 前缀扫描（66 67 F0 F2 F3 段前缀 REX）
   │     ├─ Opcode Map 选择（primary / twobyte / 0F38 / 0F3A）
   │     ├─ 组扩展（G_1 G_2 G_3 G_5 G_11 G_1A G_15 G_X87）
   │     ├─ ModRM / SIB / disp / imm
   │     └─ 填充 XemuInst（含 illegal 判定）
   │
   ├─► xemu_exec(&inst)                              exec.c
   │     ├─ x87  → fpu.c
   │     ├─ SSE  → sse.c
   │     ├─ 读操作数  read_op → ld   → xemu_vread  ──► #PF
   │     ├─ 运算（ALU / flags / 串 / 控制 / 特权）
   │     └─ 写回     write_op → st  → xemu_vwrite ──► #PF
   │
   ├─► 收尾（关键不变量）
   │     ├─ 若已投递异常 → 禁止 rip += len，直接返回
   │     ├─ 若取数/存数缺页 → 禁止 rip += len
   │     └─ 否则 rip += inst.len
   │
   └─► xemu_after_inst()                             exec.c
         ├─ xemu_timer_tick()     内部定时器
         ├─ xemu_pit_tick()       8254 计数 → IRQ0
         ├─ xemu_uart_poll()      串口输入（限频）
         └─ 若 IF=1 → xemu_pic_ack() → xemu_raise_exc(向量)
```

**这一层的两条不变量是本项目最易出错的地方**（都曾导致内核启动失败）：

1. 异常投递后 RIP 已指向处理器入口，收尾不得再加 `len`；
2. `CR2` 必须对读/写/取指**三类**故障都写入。

---

## 5. 存储与地址层

```
  客户机虚拟地址
        │
        ├── CR0.PG=0 ─────────────────────────┐
        │                                     │
        └── CR0.PG=1                          │
              │                               │
              ▼                               │
        ┌───────────┐  命中                  │
        │   TLB     │──────────────┐          │
        │ 1024 项   │              │          │
        └─────┬─────┘              │          │
              │ 未命中              │          │
              ▼                    │          │
        ┌────────────────────┐     │          │
        │ 4 级页表遍历         │     │          │
        │ PML4→PDPT→PD→PT     │     │          │
        │ 支持 1G/2M 大页      │     │          │
        │ 检查 P/RW/US/NX      │     │          │
        └─────┬──────────────┘     │          │
              │ 失败 → 置 CR2      │          │
              │        投递 #PF     │          │
              ▼                    ▼          ▼
        ┌──────────────────────────────────────────┐
        │             物理地址空间                   │
        │  [0, MEM_SIZE)         RAM                │
        │  0xE0000000            内部定时器 MMIO     │
        │  0xF0000000            停机 MMIO（退出码） │
        └──────────────────────────────────────────┘
```

- **TLB 的必要性**：内核会先清零 `early_top_pgt` 再重建，期间靠 TLB 中的旧翻译继续执行。
  TLB 在 `mov cr3` 与 `invlpg` 时刷新，与 x86 的失效规则一致。
- **CR2 由分页器自己写**，而不是调用方写。取指路径绕过 `vaccess()`，
  这个缺陷曾让内核在按需建表时补错页。
- 异常帧：同级陷阱也压入 SS:RSP（与 RTL 微码保持一致），供锁步逐字节比对。

---

## 6. 三个使用形态如何复用同一核心

```
                       ┌──────────────────────┐
                       │   XEMU 核心 (L1–L3)   │
                       └────┬──────────┬──────┘
                            │          │
        ┌───────────────────┘          └────────────────────┐
        ▼                                                    ▼
┌──────────────────┐                            ┌──────────────────────┐
│ difftest.c (.so) │                            │      main.c (bin)    │
│                  │                            │                      │
│ difftest_exec(1) │                            │ step_once() 循环      │
│ 逐指令推进        │                            │ --max-steps / -b     │
└────────┬─────────┘                            └──────────┬───────────┘
         │                                                 │
   ┌─────▼──────┬──────────────┐                    ┌──────▼──────────┐
   ▼            ▼              ▼                    ▼                 ▼
 RTL 锁步    Bochs 差分    Unicorn ISA 差分      裸机测试镜像    Linux 内核
(sys-lockstep (bochs-test)  (isa-diff)          (xemu_regress)  (--kernel)
 -test)
```

关键点：`.so` 与独立可执行文件**共用同一份目标文件和同一个 `XemuState`**，
所以「参考模型的行为」与「CLI 跑出来的行为」不会分叉。

---

## 7. 验证架构（黄金基准与测试层级）

四个独立基准，仲裁优先级见 `docs/README.md`（**Intel SDM > Bochs > QEMU > Unicorn**）：

| 基准 | 覆盖 | 入口 |
| --- | --- | --- |
| Intel SDM | 语义仲裁 | 人工查阅 |
| Bochs 2.8 | 系统态逐指令（独立解释器，非 TCG） | `make bochs-test` |
| Verilator RTL | 与自家 RTL 逐指令锁步 | `make sys-lockstep-test` |
| Unicorn 2.1.4 | 用户态 ISA 快速判定 | `make isa-diff` |

测试层级自下而上：

```
 L4  真实内核          Linux 启动（--kernel）              ← 集成，尚在攻关
 L3  系统合成场景      t60 #PF/CR2、t61 早期 IDT 桩         tests/system/*.S
 L2  单机制            t40 CPL3/TSS、t41 定时器、
                       t50 UART、t51 PIT、t52 端口
 L1  指令语义          isa-diff / emu-test / bochs-test     tests/asm、tests/emu
 L0  解码器单测        decode_test / decode_field_test      xemu/decode_test.c
```

系统测试全部是**自检汇编**：跑完后向停机 MMIO 写入退出码，
非 0 即失败，不依赖外部脚本判断。

---

## 8. 构建产物与目标

```
make xemu-so    → build/ref/libxemu.so     差分参考库
make xemu-bin   → build/xemu               独立模拟器
make isa-diff   → 对 Unicorn 跑 ISA 差分
make emu-test   → 对 RTL 跑用户态逐指令
make bochs-test → 对 Bochs 跑系统态逐指令
make xemu-sys-test      → 系统自检汇编
make sys-lockstep-test  → 对 RTL 跑系统态锁步
make xemu-all-test      → 以上 XEMU 套件合并
```

内存大小在编译期定死（`XEMU_MEM_SIZE_MB`，默认 16 MiB）：
物理内存是静态数组，避免动态分配，也让差分快照可以整块 `memcpy`。
**跑 Linux 内核必须重建**：`rm -f build/xemu && make XEMU_MEM_MB=512 xemu-bin`。

---

## 9. 已知设计偏离（有意的取舍）

| 项 | 现状 | 原因 |
| --- | --- | --- |
| 异常帧 | 同级陷阱也压 SS:RSP | 与 RTL 微码逐字节对齐，保锁步 |
| `HLT` | 不终止模拟，仅置 `halted_cpu` | 内核 idle 循环会执行 `HLT` |
| x87 精度 | 用 `long double` 承载，写回时显式舍入到 80 位 | AArch64 上 `long double` 是 binary128，精度足够 |
| 指令缓存 | 无，`CLFLUSH`/`PREFETCH` 空操作 | 单核解释执行，无自修改码场景 |
| 设备 | 无 ACPI / ACPI 表、无 APIC | 单核最小系统；串口靠 `console=ttyS0` 显式指定 |
| x87 超越函数 | `FSIN`/`FCOS`/`FPTAN`/`F2XM1` 等用宿主 `libm` 计算 | 结果与真机可能在末位有差异，足够跑用户态 |
| x87 精度控制 | 不模拟 PC 字段引起的逐步舍入 | 单精度运算在 double 路径上完成 |

---

## 10. 已知缺口（按层）

| 层 | 缺口 | 影响 |
| --- | --- | --- |
| L4 | 无 ACPI SPCR / PNP，8250 依赖 `earlyprintk`/`console=ttyS0` 显式声明 | ✅ 已解决：earlycon 与 ttyS0 console 都能工作 |
| L4 | 16550 发送中断（THRE） | ✅ 已实现：`write(2)` 到 `/dev/console` 可完整输出 |
| L3 | `rep` 串操作中途缺页 | ✅ `copy_bootdata` 路径已打通（异常帧改虚拟写） |
| L3 | `SYSENTER/SYSEXIT` 已实现（CPUID 声明 SEP） | — |
| L3 | x87 寄存器形式（D9–DF, mod=3） | ✅ 已修复：T86 覆盖 16 项，见 CHANGELOG |
| L1 | 页表修改依赖显式 `invlpg`/`cr3` 刷新，无页结构缓存建模 | 若客户机依赖未失效的旧项会与真机不同 |
