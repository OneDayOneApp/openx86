# XEMU Linux 启动开发计划（v3）

目标：让 XEMU 能按 Linux 官方 **64-bit Boot Protocol** 直接引导一个
`vmlinuz`/`vmlinux` + initramfs，跑到串口控制台与用户态 `/init`（静态 busybox shell）。

对齐 [roadmap.md](roadmap.md) 的 P4（NEMU 工具链）与 P5（设备），本文件把它们
重排为面向"能跑 Linux"的 L0–L4 里程碑。

测试用例与回归门禁见 [linux-testplan.md](linux-testplan.md)。

---

## 1. 范围与非目标

### 目标（v3 交付）

- Linux 6.x x86-64，`XLF_KERNEL_64` 入口，无 32 位 boot stub
- 串口控制台 `console=ttyS0`，可交互（至少能跑完 init 并进 shell）
- 定时器中断驱动的调度（8254 PIT → 8259 PIC → IRQ0）
- 分页 + 用户态隔离（U/S、CR0.WP、A/D）
- 静态 busybox initramfs

### 非目标（明确不做）

| 项 | 原因 |
| --- | --- |
| 实模式 / 32 位保护模式 | 用 64-bit boot protocol 完全绕开 |
| APIC / IOAPIC / ACPI | cmdline 关掉，且 CPUID 不暴露 APIC |
| 磁盘 / 块设备 / 文件系统 | initramfs 直接由 boot_params 提供 |
| VGA / 帧缓冲 | headless 串口 |
| 多核 SMP | 单 CPU |
| 虚拟化扩展（VMX/SVM） | 不涉及 |

---

## 2. 当前基线（v0.3 起点）

| 维度 | 现状 |
| --- | --- |
| 指令 | 146 op：整数、SSE2 子集、字符串、位操作、原子、部分系统指令 |
| ISA 对拍 | Unicorn **14300/14300** |
| 用户态锁步 | 37/37；系统锁步 10/10；XEMU 系统测试 2/2 |
| 内存 | 16MB 平坦数组 |
| 分页 | 4 级，**无 U/S、无 A/D、无 CR0.WP** |
| 特权 | CPL0/3、TSS RSP0 换栈、IDT 门属性/DPL |
| 中断 | 仅内部 timer 注入 IRQ32（非 8259 链路） |
| 端口 IO | **无** |
| 设备 | 仅 timer + halt MMIO |
| x87 | **无**（含 FXSAVE/FXRSTOR） |

---

## 3. 里程碑

### L0 — 端口 IO 与最小设备（1 周）

**交付**

- `IN`/`OUT`/`INS`/`OUTS`（8/16/32 位），端口 IO 分发框架
- 16550 UART（`0x3F8`）：THR/RBR/IER/IIR/LCR/MCR/LSR/SCR 寄存器语义
  - 写 `0x3F8` → stdout；读 `0x3F8` → stdin（非阻塞，空时 LSR.DR=0）
- 8259A PIC：主片 `0x20/0x21`、从片 `0xA0/0xA1`
  - ICW1–ICW4、OCW1（mask）、OCW2（EOI）、IRR/ISR
  - IRQ0 接 PIT，其余 mask
- 8254 PIT：`0x40–0x43`，通道 0 模式 2（rate generator），IRQ0
- 内存改为可配置（默认 256MB，`mmap` 后端；保留 16MB 兼容模式）

**验收**

- 自写 freestanding 测试内核：串口打印 + PIT 中断计数 + 切 CPL3，全部通过
- `make xemu-dev-test` 全绿
- 现有 14300 ISA 向量与全部锁步测试不回归

**风险**：端口 IO 与 MMIO 的语义差异（指令级 vs 内存映射）需统一抽象。

---

### L1 — Linux 引导协议（1–1.5 周）

**交付**

- `tools/xemu_boot.py`：vmlinux ELF 加载器
  - 解析 ELF program headers，按物理地址展开
  - 构造 `boot_params` 零页（e820、cmdline、initramfs 地址/大小、`hdr` 回填）
  - 打包 cpio initramfs
  - 设置 `%rsi = boot_params`，`jmp startup_64`
- `boot_params` 结构完整度：`e820_table`、`hdr.vid_mode`、`hdr.boot_flag=0xAA55`、
  `hdr.header=0x53726448`、`hdr.version>=0x020c`、`hdr.loadflags`（`LOADED_HIGH|KERNEL_64|CAN_USE_HEAP`）
- e820 图：`0x0–0x9FC00` usable、`0x9FC00–0xA0000` reserved、
  `0xF0000–0x100000` reserved、`0x100000–mem_end` usable
- 串口 early console：`earlyprintk=serial,ttyS0,115200`

**验收**

- 内核打印版本横幅与 `Memory:` 行
- 跑到 `Run /init as init process`

**风险**：`boot_params` 字段遗漏会导致内核静默挂起；需逐字段对照
`Documentation/arch/x86/boot.rst`。

---

### L2 — x87 与上下文切换（1–1.5 周）

**交付**

- `FXSAVE`/`FXRSTOR`（512 字节区域，含 MXCSR、x87 tag word）
- `CLTS`、`FNINIT`、`FNSTCW`/`FLDCW`、`FNSTSW`
- x87 最小集：`FLD`/`FST`/`FSTP`（m32/m64/m80）、`FILD`/`FIST`/`FISTP`
  （m16/m32/m64）、`FADD`/`FSUB`/`FMUL`/`FDIV` 及 `P` 后缀与 `I` 整数变体、
  `FCOM`/`FUCOM` 系列、`FXCH`、`FCHS`/`FABS`
  - 用 C `long double` 实现 80 位精度
  - 8 项栈 + TOP 旋转语义
- `SWAPGS`、`RDTSC`/`RDTSCP`、`LFENCE`/`SFENCE`/`MFENCE`、`PAUSE`
- `BSWAP`、`CLFLUSH`、`LMSW`、`LLDT`
- CPUID 调整：不暴露 APIC（leaf1 EDX bit9=0）、不暴露 x2APIC、暴露 TSC 与 FXSR

**验收**

- 用户态静态 `init` 能执行浮点运算并正确返回
- hash/checksum 类自检程序通过
- `make isa-diff` 全绿（x87 用 C long double 对照，不依赖 Unicorn）

**风险**：x87 tag word 与精度控制是经典难点；建议以 `fxsave` 后逐字段比对验证。

---

### L3 — 内存权限与异常完备（1 周）

**交付**

- **U/S 权限检查**：CPL3 访问 supervisor 页 → #PF（err bit2=1）
- **CR0.WP**：CPL0 写只读页 → #PF
- **A/D 位**：访问置 A，写置 D
- **#PF err code** 五位齐全（P/W/U/RSVD/I/D）
- **#GP** 完整触发点：非规范地址、非法选择子、特权指令、写只读 CR
- **#SS**、**#NP**（段不存在）、**#DF**（含 IST 栈切换）
- IST（Interrupt Stack Table）：`TSS.IST1–7`，双重故障用 IST1
- `INT n` DPL 校验（已有初版，补齐 #GP 的 err code）

**验收**

- Linux 用户态程序无法读写内核内存（触发 #PF 并收到 SIGSEGV）
- `make xemu-sys-test` 扩展到 ≥8 个用例（U/S 违例、WP 违例、#DF、IST）

**风险**：A/D 位缺失会让内核无法正确回收页，表现为随机 OOM，定位困难。

---

### L4 — 稳定性与工具链（1 周）

**交付**

- PCI 配置空间桩（`0xCF8/0xCFC` 返回全 1，或最小 host bridge）
- 更完整的 MSR：`EFER`、`STAR/LSTAR/SFMASK`、`FS/GS_BASE`、
  `TSC`、`MTRR`（读回默认值）、`PATG`、`SYSENTER_CS/ESP/EIP`
- `mtrace`/`ftrace`/`itrace` 反汇编（NEMU 风格），用于内核调试
- 性能剖析：目标 ≥ 20 MIPS（当前单线程解释执行）
- `make xemu-linux-test`：CI 门禁（串口输出匹配 + 退出码）

**验收**

- Linux 跑到 `login` 或 busybox shell，能执行 `ls`、`cat /proc/cpuinfo`、`echo`
- 连续运行 10 分钟无 hang

---

## 4. 依赖顺序

```
L0 (端口IO/UART/PIC/PIT)
      │
      ├──► L1 (boot protocol)  ──► 内核早期启动
      │
      └──► L2 (x87/FXSAVE)     ──► 用户态
                  │
                  └──► L3 (U/S 权限) ──► 进程隔离
                              │
                              └──► L4 (工具链/稳定) ──► 可交互 shell
```

L1 与 L2 可并行；L3 必须在 L1 之后（需要内核页表）。

---

## 5. 验收矩阵

| 维度 | L0 | L1 | L2 | L3 | L4 |
| --- | --- | --- | --- | --- | --- |
| 串口输出 | ✅ | ✅ | ✅ | ✅ | ✅ |
| 内核启动至 init | — | ✅ | ✅ | ✅ | ✅ |
| 用户态执行 | — | — | ✅ | ✅ | ✅ |
| 进程隔离 | — | — | — | ✅ | ✅ |
| 交互 shell | — | — | — | — | ✅ |
| ISA diff 不回归 | ✅ | ✅ | ✅ | ✅ | ✅ |
| 系统锁步不回归 | ✅ | ✅ | ✅ | ✅ | ✅ |

---

## 6. 工作量汇总

| 里程碑 | 新代码量（估） | 周期 |
| --- | --- | --- |
| L0 | 900–1200 行 | 1 周 |
| L1 | 400–600 行 | 1–1.5 周 |
| L2 | 900–1400 行 | 1–1.5 周 |
| L3 | 500–700 行 | 1 周 |
| L4 | 600–900 行 | 1 周 |
| **合计** | **3300–4800 行** | **5–6 周** |

---

## 7. 风险与对策

| 风险 | 影响 | 对策 |
| --- | --- | --- |
| `boot_params` 字段错误 | 内核静默挂起 | 逐字段对照 `boot.rst`；先跑 `tools/xemu_boot.py --dump` 自检 |
| x87 tag word / 精度 | 用户态计算错乱 | 用 C `long double` + 逐指令 `fxsave` 比对 |
| 32 位操作数语义隐藏 bug | 内核路径随机崩溃 | 内核启动前先用 ISA diff 覆盖 32 位路径 |
| 无 QEMU 对照 | 定位困难 | 先做 mtrace/ftrace/itrace；用 known-good initramfs 做黑盒对比 |
| A/D 位缺失 | 随机 OOM | L3 前先加断言检测 |
| 解释器性能不足 | 启动太慢（分钟级） | 先接受；必要时加基本块缓存（L4 之后） |

---

## 8. 立即可执行（L0 Sprint 1）

1. 端口 IO 框架 + `IN`/`OUT` 指令（含 Unicorn 对拍补充）
2. 16550 UART，`xemu -b` 输出直通 stdout
3. 8259 PIC + 8254 PIT，替换现有 timer 注入路径
4. 内存可配置化（256MB）
5. 自写 freestanding 测试内核 `tests/xkernel/`（串口 + 中断 + CPL3）
6. `make xemu-dev-test`
