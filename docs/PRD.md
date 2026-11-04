# X86-KMH 处理器产品需求文档（PRD）

| 项目 | 内容 |
| --- | --- |
| 产品代号 | X86-KMH（X86 core, KunMingHu-inspired） |
| 文档版本 | v0.1（初稿，待评审） |
| 文档日期 | 2026-11-03 |
| 文档状态 | Draft |
| 目标读者 | 架构、RTL、验证、编译器/工具链、项目管理者 |
| 参考微架构 | 香山昆明湖 V3（Kunminghu V3） |
| 参考指令集 | Intel SDM Vol.1/2/3、AMD APM Vol.2/3（x86-64 长模式） |
| 实现语言 | Chisel 3.5.6（Scala 2.13.10），生成 SystemVerilog |
| 仿真环境 | Verilator 5.04 + Unicorn 2.1.4 参考模型 |

修订历史：

| 版本 | 日期 | 修订人 | 说明 |
| --- | --- | --- | --- |
| v0.1 | 2026-11-03 | 架构组 | 首版：范围、ISA、微架构、验证、里程碑 |

---

## 目录

1. [背景与目标](#1-背景与目标)
2. [术语与参考资料](#2-术语与参考资料)
3. [用户、场景与价值](#3-用户场景与价值)
4. [产品范围与版本规划](#4-产品范围与版本规划)
5. [指令集需求](#5-指令集需求)
6. [微架构需求（参考昆明湖 V3）](#6-微架构需求参考昆明湖-v3)
7. [接口需求](#7-接口需求)
8. [验证需求](#8-验证需求)
9. [里程碑与验收标准](#9-里程碑与验收标准)
10. [性能需求](#10-性能需求)
11. [非功能需求](#11-非功能需求)
12. [风险与对策](#12-风险与对策)
13. [交付物清单](#13-交付物清单)
14. [需求追踪矩阵](#14-需求追踪矩阵)
15. [附录](#15-附录)

---

## 1. 背景与目标

### 1.1 背景

x86-64 是服务器、PC、工控与教学领域事实上的主流指令集，但其高性能实现长期由少数厂商垄断，公开的、可综合、可复现的 x86-64 乱序核 RTL 极少。与此同时，香山（XiangShan）昆明湖 V3 以开源方式完整实践了现代高性能乱序处理器的微架构方法论：多级分支预测、变长/定长前端、统一物理寄存器堆、压缩 ROB、分布式发射队列、访存依赖预测、快照恢复等。

本项目希望回答一个工程问题：**以 x86-64 指令集文档为准绳，以昆明湖 V3 的微架构组织方式为参照，能否用 Chisel 构建一个结构完整、行为可验证、可综合的 x86-64 处理器？**

项目不追求首版达到商业 x86 的性能，而追求三件事：

1. **架构正确性**：与参考模型逐指令对拍（difftest）无差异；
2. **结构完整性**：覆盖 x86-64 长模式下整数 + SSE2 的核心子系统（前端、乱序后端、访存、MMU、异常/中断）；
3. **可演进性**：从顺序流水线平滑演进到乱序超标量，参数化可配置，文档可复现。

### 1.2 产品目标

| 编号 | 目标 | 度量 |
| --- | --- | --- |
| G-1 | 交付可仿真的 x86-64 顺序核（阶段 A） | 通过 P0 整数测试集，difftest 10^6 条指令零差异 |
| G-2 | 交付 x86-64 + SSE2 的顺序核（阶段 A+） | P0 SSE2 测试全通过，异常/分页测试全通过 |
| G-3 | 演进为昆明湖 V3 风格乱序核（阶段 B） | 重命名/ROB/发射队列/PRF/LSU 全链路打通，difftest 零差异 |
| G-4 | 性能可度量 | 乱序核在整数混合负载上 IPC ≥ 1.5× 顺序核；分支 MPKI 相对静态预测下降 ≥ 50% |
| G-5 | 工程可复现 | 一条命令完成 RTL 生成、仿真、回归；文档覆盖架构、微架构、验证、使用 |

### 1.3 成功标准（North Star）

> 在一台普通开发机上，执行 `make regress`，能够从 Chisel 源码生成 Verilog、用 Verilator 编译、运行全部汇编测试与 difftest，输出 PASS 报告；报告中包含指令覆盖、IPC、分支预测命中率与关键微架构计数器。

### 1.4 已确认的关键决策

| 编号 | 决策 | 结论 | 确认方式 |
| --- | --- | --- | --- |
| D-1 | 指令集范围 | x86-64 长模式 + SSE2 浮点子集 | 需求方确认 |
| D-2 | 微架构路线 | 分阶段：先顺序流水线跑通，再演进到乱序超标量 | 需求方确认 |
| D-3 | 实现语言 | Chisel（Scala），生成 SystemVerilog | 需求方确认 |
| D-4 | 验证方式 | Verilator 仿真 + 汇编测试集 + Python 参考模型对拍 | 需求方确认 |
| D-5 | 参考微架构 | 香山昆明湖 V3（结构对齐、参数按 x86 现实裁剪） | 需求方确认 |

### 1.5 非目标（首版明确不做）

- 不追求流片级物理实现（不做 DFT、时钟树、功耗签核、封装）；
- 不支持 x87、MMX、AVX/AVX2/AVX-512、SSE3 及以上；
- 不支持 16 位实模式、32 位兼容模式、传统分段内存模型；
- 不支持 SMT、多核、缓存一致性协议；
- 不支持 VT-x/SVM 虚拟化、SGX/SEV 等机密计算；
- 首版不承诺启动 Linux（见 §4.4 可选扩展）；
- 不做 Spectre/Meltdown 等推测执行侧信道加固（列为 P2 需求）。

---

## 2. 术语与参考资料

### 2.1 术语

| 术语 | 含义 |
| --- | --- |
| Long mode | x86-64 的 64 位运行模式，本产品唯一支持的运行模式 |
| uop | 微操作，处理器内部执行单元；一条 x86 指令可拆为 1~N 个 uop |
| Macro-op | 译码后的一条指令级微操作（本项目中多数指令为单 uop） |
| ROB | Reorder Buffer，重排序缓冲，保证按序提交与精确异常 |
| PRF | Physical Register File，物理寄存器堆 |
| RAT | Register Alias Table，寄存器别名表（逻辑→物理映射） |
| IQ | Issue Queue，发射队列 |
| LSQ | Load/Store Queue，访存队列 |
| BPU | Branch Prediction Unit，分支预测单元 |
| FTQ | Fetch Target Queue，取指目标队列 |
| Difftest | 差分测试：RTL 与参考模型逐指令比对架构状态 |
| MPKI | Misses Per Kilo Instructions，每千条指令缺失数 |
| IPC | Instructions Per Cycle，每周期退休指令数 |

### 2.2 参考资料

| 编号 | 资料 | 用途 |
| --- | --- | --- |
| R-1 | Intel 64 and IA-32 Architectures Software Developer's Manual, Vol.1-3 | 指令语义、编码、异常、分页、CPUID |
| R-2 | AMD64 Architecture Programmer's Manual, Vol.2-3 | 长模式系统编程、SYSCALL/SYSRET |
| R-3 | 香山开源处理器设计文档（昆明湖 V3）<https://docs.xiangshan.cc/projects/design/zh-cn/kunminghu-v3/> | 微架构组织、参数、设计权衡 |
| R-4 | 香山微架构课程（本地 `xiangshan-course/docs/4-xiangshan-microarchitecture-analysis/`） | 重命名、分派、发射、ROB、LSU 细节 |
| R-5 | 《计算机体系结构：量化研究方法》 | 流水线、乱序、存储层次方法论 |
| R-6 | Unicorn Engine 2.x 文档 | 参考模型与 difftest |
| R-7 | RISC-V 特权规范（对照参考） | 异常/中断/MMU 设计对照 |

> 昆明湖 V3 公开参数（用于对标）：6 宽译码/重命名/分派；224 项整数 PRF、192 项浮点 PRF、128 项向量 PRF；160 项 ROB（支持压缩，单项至多 6 个 uop）；每周期最多退休 8 项；256 项 RAB；Move 消除与指令融合；BPU 采用 uBTB/aBTB/uTAGE/mBTB/TAGE/SC/ITTAGE/RAS 多级结构；预测块最多 32B/16 条指令，支持双 cacheline 取指；LSU 3×Load + 2×Store；DCache 64KB 4 路；L1 TLB 48 项全相联；SBuffer 16 项。

---

## 3. 用户、场景与价值

### 3.1 目标用户

| 用户 | 诉求 |
| --- | --- |
| 体系结构研究者 | 可修改、可参数化的 x86 乱序核，用于研究分支预测、访存依赖、前端宽度等 |
| 高校教学 | 从顺序核到乱序核的渐进式实验平台，配套中文文档与波形 |
| SoC/FPGA 原型团队 | 可综合 RTL、标准存储器接口、可裁剪的核 |
| 编译器/工具链开发者 | 可运行的 x86 子集目标，用于验证代码生成与 ABI |

### 3.2 典型场景

1. **裸机测试**：加载 flat binary 到 `0x400000`，栈顶 `0x800000`，执行到 `0xF0000000` MMIO 写入即停机并返回退出码。
2. **逐指令对拍**：RTL 提交级 trace 与 Unicorn 参考模型比对 GPR/XMM/RIP/RFLAGS/内存写。
3. **性能分析**：统计 IPC、分支预测准确率、L1 命中率、ROB 占用、发射停顿原因。
4. **FPGA 原型**（P1）：在 Xilinx/Intel FPGA 上以 50–100 MHz 运行裸机测试。

### 3.3 产品价值

- 首个公开的、以香山 V3 方法学组织的 x86-64 乱序核参考实现；
- 一套可复用的“变长 ISA + 乱序后端”工程模板；
- 为 x86 与 RISC-V 微架构对比研究提供同构基线。

---

## 4. 产品范围与版本规划

### 4.1 版本路线

| 版本 | 内容 | 对应里程碑 |
| --- | --- | --- |
| V0.1 | 工具链、工程骨架、解码器 | M0–M1 |
| V0.2 | 顺序五级流水整数核 + difftest | M2 |
| V0.3 | SSE2、分页、异常/中断、SYSCALL | M3 |
| V0.5 | 乱序前端（BPU、FTQ、重命名、ROB、分派） | M4 |
| V0.9 | 乱序后端（IQ、执行单元、PRF、LSU、提交） | M5 |
| V1.0 | 性能调优、综合检查、文档与回归定版 | M6 |
| V1.x（可选） | uop cache、2MB 大页、Linux bring-up、AXI4、FPGA | 后续 |

### 4.2 范围矩阵

| 能力 | V0.2 | V0.3 | V1.0 | 说明 |
| --- | --- | --- | --- | --- |
| x86-64 整数指令 | ✅ | ✅ | ✅ | P0 子集 |
| SSE2 标量/向量 | — | ✅ | ✅ | P0 子集 |
| 4 级分页 / 4KB 页 | — | ✅ | ✅ | P0 |
| 2MB/1GB 大页 | — | P1 | P1 | 见 §5.5 |
| 异常 / IDT / 中断 | 部分 | ✅ | ✅ | 精确异常 |
| SYSCALL/SYSRET | — | ✅ | ✅ | ring 0 内部使用 |
| 乱序执行 | — | — | ✅ | 阶段 B |
| 分支预测 | 静态 | 静态 | 动态多级 | 对齐 V3 结构 |
| L1 I/D Cache | 简化 | 简化 | ✅ | 首版可直连存储器 |
| L2 Cache | — | — | P1 | 512KB 目标 |
| uop cache | — | — | P1 | x86 前端关键优化 |
| 多核/一致性 | — | — | — | 非目标 |

### 4.3 运行环境假设

- 仅支持 **64 位长模式**，CPL=0（ring 0）运行；不实现 ring 1/2/3 切换（P2 可加 ring 3）。
- 复位后 `RIP = 0x400000`（测试约定），`RSP = 0x800000`，`CR0.PG=0` 起始；由启动代码建立页表后开启分页。
- 存储器模型：单核、统一物理地址空间、无缓存一致性；I/D 分离的 L1 接口。
- 无传统 8259 PIC/IOAPIC，首版中断由外部 `intr` 端口注入，IDT 交付；P1 增加 Local APIC 简化模型。

### 4.4 可选扩展（V1.x）

- 启动 Linux：需要完整系统指令、ACPI/APIC、PCIe 枚举、设备模型，工作量约为当前范围 2–3 倍，单独立项。
- 2MB 大页与 NX：页表 walker 小改，收益明显，建议 V1.1。
- uop cache：x86 前端命中率关键，建议 V1.2。

---

## 5. 指令集需求

### 5.1 需求编号规则

`REQ-<类别>-<序号>`；优先级：**P0** 必须（V1.0 验收）、**P1** 应该（V1.x）、**P2** 可选（远期）。

### 5.2 运行模式与寄存器

| 编号 | 需求 | 优先级 |
| --- | --- | --- |
| REQ-ISA-001 | 仅支持 IA-32e 长模式（64 位代码段，L=1），不支持实模式/保护模式/兼容模式 | P0 |
| REQ-ISA-002 | 16 个 64 位通用寄存器 RAX…R15；32 位写零扩展高 32 位；8/16 位写保留高位 | P0 |
| REQ-ISA-003 | RIP、RFLAGS（CF/PF/AF/ZF/SF/TF/IF/DF/OF 及 IOPL 只读位） | P0 |
| REQ-ISA-004 | 16 个 128 位 XMM 寄存器；MXCSR（默认 0x1F80） | P0 |
| REQ-ISA-005 | 控制寄存器 CR0/CR2/CR3/CR4；EFER（SCE/NXE/LME/LMA） | P0 |
| REQ-ISA-006 | 系统寄存器：IDTR/GDTR（最小）、STAR/LSTAR/SFMASK、FS_BASE/GS_BASE（P1） | P0/P1 |
| REQ-ISA-007 | 高 8 位寄存器 AH/BH/CH/DH 在无 REX 时可用；REX 存在时 4–7 号编码为 SPL/BPL/SIL/DIL | P0 |
| REQ-ISA-008 | 规范地址检查（48 位符号扩展）；非规范地址访问触发 #GP | P0 |

### 5.3 指令集清单

#### 5.3.1 整数数据传送（REQ-ISA-100 系列）

| 编号 | 指令 | 说明 | 优先级 |
| --- | --- | --- | --- |
| REQ-ISA-101 | MOV r/m, r；MOV r, r/m；MOV r, imm | 8/16/32/64 位，含 `B8+r` imm64 | P0 |
| REQ-ISA-102 | MOVZX / MOVSX / MOVSXD | 8/16→32/64，32→64 | P0 |
| REQ-ISA-103 | LEA | 含 RIP 相对、SIB | P0 |
| REQ-ISA-104 | XCHG | 寄存器/寄存器、寄存器/内存 | P0 |
| REQ-ISA-105 | PUSH / POP（含 r/m64、imm） | 64 位默认操作数 | P0 |
| REQ-ISA-106 | MOV moffs（A0–A3） | 绝对地址传送 | P1 |
| REQ-ISA-107 | CMOVcc | 16 种条件 | P0 |
| REQ-ISA-108 | SETcc | 16 种条件，字节写 | P0 |

#### 5.3.2 整数运算（REQ-ISA-200 系列）

| 编号 | 指令 | 说明 | 优先级 |
| --- | --- | --- | --- |
| REQ-ISA-201 | ADD/ADC/SUB/SBB/CMP | 全宽度，设置 6 个算术标志 | P0 |
| REQ-ISA-202 | INC/DEC/NEG/NOT | INC/DEC 不改 CF | P0 |
| REQ-ISA-203 | AND/OR/XOR/TEST | 逻辑标志（CF=OF=0，AF 未定义） | P0 |
| REQ-ISA-204 | MUL/IMUL（1/2/3 操作数） | 64×64→128（RDX:RAX） | P0 |
| REQ-ISA-205 | DIV/IDIV | 64÷64，商溢出/除零 → #DE | P0 |
| REQ-ISA-206 | SHL/SHR/SAR/ROL/ROR/RCL/RCR | 计数来自 imm8/1/CL，按宽度掩码 | P0 |
| REQ-ISA-207 | CBW/CWDE/CDQE；CWD/CDQ/CQO | 符号扩展 | P0 |
| REQ-ISA-208 | BT/BTS/BTR/BTC | 位测试与修改 | P1 |
| REQ-ISA-209 | BSF/BSR | 位扫描 | P1 |
| REQ-ISA-210 | POPCNT/LZCNT/TZCNT | 需 CPUID 声明 | P1 |
| REQ-ISA-211 | CMPXCHG / XADD | 原子语义在单核下简化为不可中断 | P1 |
| REQ-ISA-212 | RDRAND/RDSEED | 确定性伪随机（测试可复现） | P2 |

#### 5.3.3 控制流（REQ-ISA-300 系列）

| 编号 | 指令 | 说明 | 优先级 |
| --- | --- | --- | --- |
| REQ-ISA-301 | JMP rel8/rel32；JMP r/m64 | 直接/间接跳转 | P0 |
| REQ-ISA-302 | Jcc rel8/rel32 | 16 种条件 | P0 |
| REQ-ISA-303 | CALL rel32；CALL r/m64；RET；RET imm16 | 64 位栈操作 | P0 |
| REQ-ISA-304 | LOOP/LOOPE/LOOPNE/JRCXZ | 计数循环 | P0 |
| REQ-ISA-305 | 条件码语义完整（含 AF/PF） | 与 SDM 一致 | P0 |
| REQ-ISA-306 | 间接跳转/调用的 BTB 预测（阶段 B） | 目标地址预测 | P1 |

#### 5.3.4 字符串操作（REQ-ISA-400 系列）

| 编号 | 指令 | 说明 | 优先级 |
| --- | --- | --- | --- |
| REQ-ISA-401 | MOVS/STOS/LODS/SCAS/CMPS | 8/16/32/64 元素 | P0 |
| REQ-ISA-402 | REP/REPE/REPNE 前缀 | RCX 计数，DF 方向 | P0 |
| REQ-ISA-403 | 字符串操作可被中断/异常精确打断 | 保存 RCX/RSI/RDI 进度 | P1 |

#### 5.3.5 系统指令（REQ-ISA-500 系列）

| 编号 | 指令 | 说明 | 优先级 |
| --- | --- | --- | --- |
| REQ-ISA-501 | SYSCALL/SYSRET | STAR/LSTAR/SFMASK；ring 0 内部快速调用 | P0 |
| REQ-ISA-502 | CPUID | 叶子 0/1/0x80000000/0x80000001/0x80000008 | P0 |
| REQ-ISA-503 | RDTSC/RDTSCP | 周期计数器；测试可注入固定值 | P1 |
| REQ-ISA-504 | HLT | 停机，等待中断或测试结束 | P0 |
| REQ-ISA-505 | INT n / INT3 / INTO(非法) / IRETQ | IDT 交付与返回 | P0 |
| REQ-ISA-506 | CLI/STI/PUSHF/POPF/LAHF/SAHF | 标志寄存器读写；ring 0 下 IF 可改 | P0 |
| REQ-ISA-507 | CLD/STD/CMC/CLC/STC | 标志位操作 | P0 |
| REQ-ISA-508 | RDMSR/WRMSR | 至少 EFER/STAR/LSTAR/SFMASK | P0 |
| REQ-ISA-509 | INVLPG / MOV CR3 | TLB 失效 | P0 |
| REQ-ISA-510 | UD2 | 显式 #UD | P0 |
| REQ-ISA-511 | ENDBR64 | 作为 NOP（CET 不启用） | P1 |
| REQ-ISA-512 | RDPMC、性能计数器 | 微架构计数器 | P2 |

#### 5.3.6 SSE2（REQ-ISA-600 系列）

| 编号 | 指令 | 说明 | 优先级 |
| --- | --- | --- | --- |
| REQ-ISA-601 | MOVAPS/MOVUPS/MOVDQA/MOVDQU/MOVSS/MOVSD | 128 位及标量装载/存储 | P0 |
| REQ-ISA-602 | MOVD/MOVQ（GPR↔XMM） | 32/64 位 | P0 |
| REQ-ISA-603 | ADDPS/ADDPD/ADDSS/ADDSD、SUB/MUL/DIV 同族 | IEEE 754 四则 | P0 |
| REQ-ISA-604 | SQRTPS/SQRTPD/SQRTSS/SQRTSD | 平方根 | P0 |
| REQ-ISA-605 | MINPS/PD/SS/SD、MAXPS/PD/SS/SD | NaN/±0 语义 | P0 |
| REQ-ISA-606 | ANDPS/PD、ANDNPS/PD、ORPS/PD、XORPS/PD | 位逻辑 | P0 |
| REQ-ISA-607 | PADDD/PSUBD/PAND/POR/PXOR | 128 位整数向量 | P0 |
| REQ-ISA-608 | COMISS/COMISD/UCOMISS/UCOMISD | 设置 ZF/PF/CF | P0 |
| REQ-ISA-609 | CVTSI2SS/SD、CVTTSS2SI/SD、CVTSS2SI/SD、CVTSS2SD、CVTSD2SS | 标量转换 | P0 |
| REQ-ISA-610 | CVTDQ2PS、CVTPS2DQ、CVTDQ2PD、CVTPD2DQ | 向量转换 | P1 |
| REQ-ISA-611 | SHUFPS/SHUFPD/PSHUFD、UNPCK*、PUNPCK* | 重排 | P1 |
| REQ-ISA-612 | PSLLD/PSRLD/PSRAD 等移位 | 向量移位 | P1 |
| REQ-ISA-613 | MASKMOVDQU、MOVMSKPS/PD、PMOVMSKB | 掩码/提取 | P1 |
| REQ-ISA-614 | LDMXCSR/STMXCSR | MXCSR 读写 | P1 |

**明确不支持**：x87（D8–DF）、MMX（无前缀的 0F 6x/7x 等）、SSE3/SSSE3/SSE4、AVX/AVX2/AVX-512、FMA、AES/SHA、BMI1/2（P2 可选）、CET、MPX、TSX。

### 5.4 编码需求

| 编号 | 需求 | 优先级 |
| --- | --- | --- |
| REQ-ENC-001 | 支持 legacy 前缀：0x66/0x67/0xF0/0xF2/0xF3/段前缀（段基址忽略，FS/GS 见 P1） | P0 |
| REQ-ENC-002 | 支持 REX 前缀（40–4F），REX.W/R/X/B；REX 后出现 legacy 前缀则 REX 失效 | P0 |
| REQ-ENC-003 | 支持 ModRM/SIB/disp8/disp32；RIP 相对寻址（mod=00, r/m=101） | P0 |
| REQ-ENC-004 | 支持一字节与 0F 两字节 opcode map；0F38/0F3A 首版 #UD | P0 |
| REQ-ENC-005 | 指令长度 1–15 字节；超过 15 字节或跨页取指触发 #GP（跨页 P1） | P0 |
| REQ-ENC-006 | 立即数小端序；imm8/imm16/imm32/imm64 按操作数与 REX.W 选择 | P0 |
| REQ-ENC-007 | 64 位模式默认操作数 32 位；0x66→16；REX.W→64；32 位写零扩展 | P0 |
| REQ-ENC-008 | 地址默认 64 位；0x67→32 位地址（零扩展） | P1 |
| REQ-ENC-009 | LOCK 前缀在单核下作为内存屏障语义（不产生总线锁） | P1 |

### 5.5 特权、异常与分页

| 编号 | 需求 | 优先级 |
| --- | --- | --- |
| REQ-SYS-001 | 异常：#DE(0)、#BP(3)、#UD(6)、#DF(8)、#GP(13)、#PF(14)、#AC(17，P2) | P0 |
| REQ-SYS-002 | 64 位 IDT 门描述符（16 字节），中断/陷阱门，DPL 检查 | P0 |
| REQ-SYS-003 | 异常交付：压入 SS/RSP/RFLAGS/CS/RIP（及错误码），跳转 handler | P0 |
| REQ-SYS-004 | IRETQ 完整恢复 | P0 |
| REQ-SYS-005 | 4 级页表（PML4→PDPT→PD→PT），4KB 页；CR3 物理地址 | P0 |
| REQ-SYS-006 | 页错误错误码（P/W/U/RSVD/I/D） | P0 |
| REQ-SYS-007 | 2MB/1GB 大页 | P1 |
| REQ-SYS-008 | NX 位（EFER.NXE） | P1 |
| REQ-SYS-009 | CR0.WP、CR0.PG、CR4.PAE（必须为 1）、CR4.PGE（P1） | P0 |
| REQ-SYS-010 | TLB：ITLB/DTLB，INVLPG 与 CR3 写失效 | P0 |
| REQ-SYS-011 | 外部中断注入（INTR/NMI 端口），IF 屏蔽，边沿/电平约定 | P0 |
| REQ-SYS-012 | 中断优先级与异常优先级符合 SDM（#DF > 其他） | P0 |
| REQ-SYS-013 | 双故障（#DF）兜底，不可恢复时停机并报告 | P1 |

### 5.6 浮点语义

| 编号 | 需求 | 优先级 |
| --- | --- | --- |
| REQ-FP-001 | 单/双精度 IEEE 754 加减乘除、开方，默认 round-to-nearest-even | P0 |
| REQ-FP-002 | NaN 传播：QNaN 输入产生 QNaN；SNaN 置 MXCSR.IE 并产生 QNaN | P0 |
| REQ-FP-003 | ±0、denormal、infinity 运算符合 IEEE | P0 |
| REQ-FP-004 | MXCSR 舍入模式、DAZ/FTZ、异常标志与屏蔽位 | P1 |
| REQ-FP-005 | COMISS/UCOMISS 的 ZF/PF/CF 设置与 unordered 语义 | P0 |
| REQ-FP-006 | 转换指令的截断/舍入与越界返回 0x80000000（CVTT*） | P0 |

### 5.7 CPUID 与特性声明

| 叶子 | 内容 |
| --- | --- |
| 0x00000000 | MaxBasicLeaf=0x00000001；Vendor ID = `X86-KMH-Core`（12 字节，自定义） |
| 0x00000001 | Family/Model/Stepping；EDX：TSC、MSR、PAE、CX8、SEP、CMOV、SSE、SSE2、FXSR、CLFSH（FPU/MMX 置 0；LM 见扩展叶子） |
| 0x80000000 | MaxExtLeaf=0x80000008 |
| 0x80000001 | EDX：SYSCALL、NX、LM、RDTSCP；ECX：LAHF_LM |
| 0x80000008 | 物理地址位宽（如 40）、线性地址位宽（48） |

> 说明：CPUID 必须如实反映实现，避免测试误用未实现特性。

### 5.8 指令集验收

- P0 指令覆盖率 100%（按 §5.3 表逐条有测试）；
- 每条 P0 指令至少 1 个正例 + 1 个边界例（进位/溢出/零/符号/非对齐）；
- 所有标志位组合（CF/OF/AF/ZF/SF/PF）与 Intel SDM 伪代码一致；
- 未实现编码必须产生 #UD，不得静默执行。

---

## 6. 微架构需求（参考昆明湖 V3）

### 6.1 总体策略

分两个阶段交付，阶段之间复用解码器、Uop 定义、存储子系统和验证环境：

- **阶段 A（V0.2–V0.3）**：经典五级顺序流水线（IF/ID/EX/MEM/WB），单发射，静态分支预测，目标是把 ISA 语义与验证闭环打通。
- **阶段 B（V0.5–V1.0）**：昆明湖 V3 风格乱序超标量，包含前端 BPU/FTQ、重命名、分派、分布式发射队列、统一 PRF、ROB 压缩与快照恢复、LSQ/SBuffer。

设计原则：

1. **前端与后端解耦**：解码器输出架构无关的 `Uop`，后端可替换；
2. **参数化**：宽度、队列深度、PRF 大小通过 Chisel `Parameters` 配置；
3. **可观测**：每个流水级有 valid/ready 与性能计数器，便于波形定位；
4. **先正确后性能**：阶段 A 不做推测执行；阶段 B 的所有推测必须有精确恢复路径。

### 6.2 阶段 A：顺序流水线

| 编号 | 需求 | 目标值 |
| --- | --- | --- |
| REQ-UARCH-A01 | 取指：16B/周期，跨 16B 边界时拆两次取指 | 1 |
| REQ-UARCH-A02 | 译码：1 条/周期，变长 1–15B，表驱动 | 1 |
| REQ-UARCH-A03 | 执行：整数 ALU/MUL/DIV/移位；SSE2 标量+向量 | 1 |
| REQ-UARCH-A04 | 访存：对齐/非对齐、1/2/4/8/16B；跨 8B 边界拆分 | 1 |
| REQ-UARCH-A05 | 提交：单条按序；异常在提交点精确触发 | 1 |
| REQ-UARCH-A06 | 分支：预测不跳转，EX 级解析并冲刷 | — |
| REQ-UARCH-A07 | 旁路：EX/MEM→EX 前递；load-use 停顿 1 拍 | — |
| REQ-UARCH-A08 | 多周期指令（DIV、REP 字符串）采用流水线停顿 | — |

阶段 A 的目标 IPC：0.5–0.9（受单发射与解码限制）。

### 6.3 阶段 B：乱序超标量（V3 对标）

#### 6.3.1 参数目标

| 模块 | 本项目目标（V1.0） | 昆明湖 V3 参照 | 说明 |
| --- | --- | --- | --- |
| 取指宽度 | 32B/周期 | 32B/预测块 | x86 变长，按字节取指 |
| 预测块 | 16B，最多 6 条指令 | 32B，最多 16 条 | 首版减小以简化 |
| 译码宽度 | 4 条/周期 | 6 条/周期 | 变长解码是瓶颈 |
| 重命名/分派宽度 | 4/6 uop/周期 | 6/6 | 先 4 宽，uop cache 后提升 |
| ROB | 128 项（压缩，单项 ≤4 uop） | 160 项（≤6 uop） | 参数化 |
| 退休宽度 | 4 项/周期 | 8 项/周期 | |
| 整数 PRF | 160 项 | 224 项 | 统一 PRF |
| XMM PRF | 128 项 | 192（FP） | 统一 PRF |
| 发射队列 | 2×ALU(16)、MUL/DIV(12)、2×LSU(16)、BR(12) | 分布式 IQ | 年龄矩阵选择 |
| 执行单元 | 3 ALU + 1 MUL/DIV + 2 AGU + 1 ST + 1 BR + 1 SSE | 多 ALU/FP/Vector | 首版 SSE 单端口 |
| Load/Store | 2 load + 1 store / 周期 | 3 load + 2 store | x86 非对齐代价 |
| LQ/SQ | 48 / 32 项 | LSQ 分队列 | |
| SBuffer | 16 项 × 64B | 16 项 | 合并写 |
| L1I | 32KB 8 路 | 公开文档未列 | 首版可简化 |
| L1D | 32KB 8 路 | 64KB 4 路 | |
| L2 | 512KB 8 路（P1） | CoupledL2 | 首版可无 |
| ITLB/DTLB | 32/48 项 | 48 项 | |
| L2 TLB | 1024 项 | 多级 PTW | |
| 分支预测 | uBTB+aBTB+mBTB+TAGE-SC+ITTAGE+RAS | 同构，多级 | 见 6.3.4 |

#### 6.3.2 前端（Frontend）

| 编号 | 需求 | 优先级 |
| --- | --- | --- |
| REQ-UARCH-F01 | FTQ：每项记录起始 PC、预测块字节范围、预测方向/目标、RAS 动作 | P0 |
| REQ-UARCH-F02 | IFU：按 FTQ 请求从 L1I 取 32B，处理跨 cacheline、跨页 | P0 |
| REQ-UARCH-F03 | 预解码：标注指令边界、长度、是否分支；结果可缓存（predecode cache，P1） | P0 |
| REQ-UARCH-F04 | 译码：变长解码 4 条/周期，输出 Uop；非法编码产生 #UD 标记 | P0 |
| REQ-UARCH-F05 | 分支预测：条件分支方向 + 直接跳转目标 + 间接跳转目标 + 返回地址 | P0 |
| REQ-UARCH-F06 | 预测失败恢复：重定向时冲刷前端，恢复 RAT/自由列表，从正确 PC 重取 | P0 |
| REQ-UARCH-F07 | 预测块跨越 16B 边界时支持双窗口拼接 | P0 |
| REQ-UARCH-F08 | uop cache（P1）：命中时绕过译码，4–6 uop/周期 | P1 |

**x86 特有约束**：

- 指令边界必须由长度解码确定，预测块内分支可能不是块末尾；
- 分支目标可能落在指令中间（非法），需在取指/译码阶段检测；
- 自修改代码（SMC）需要 I-Cache 与预解码缓存失效机制（P1，单核可用 `INVLPG` + 显式 fence 简化）。

#### 6.3.3 重命名与分派（Rename / Dispatch）

| 编号 | 需求 | 优先级 |
| --- | --- | --- |
| REQ-UARCH-R01 | 逻辑寄存器→物理寄存器映射（GPR 16→160，XMM 16→128） | P0 |
| REQ-UARCH-R02 | 自由列表（Free List）分配/回收，提交时回收旧物理寄存器 | P0 |
| REQ-UARCH-R03 | 源操作数就绪状态（Busy Table）与唤醒 | P0 |
| REQ-UARCH-R04 | 分支快照（Snapshot）：RAT/FreeList 检查点，误预测快速恢复 | P1 |
| REQ-UARCH-R05 | Move 消除：`mov r,r` 在重命名阶段消解 | P1 |
| REQ-UARCH-R06 | 指令融合：CMP+Jcc、TEST+Jcc 融合为单 uop | P1 |
| REQ-UARCH-R07 | 分派按 FU 类型路由到对应 IQ，并检查 ROB/LSQ 空间 | P0 |
| REQ-UARCH-R08 | 每周期 4 uop 重命名，6 uop 分派（可参数化） | P0 |

#### 6.3.4 分支预测（BPU）

| 编号 | 需求 | 优先级 |
| --- | --- | --- |
| REQ-UARCH-B01 | uBTB：极小容量、低延迟，覆盖热分支 | P0 |
| REQ-UARCH-B02 | aBTB/mBTB：多级 BTB，容量递增、延迟递增 | P1 |
| REQ-UARCH-B03 | TAGE + 统计校正器（SC）：长历史条件分支方向 | P1 |
| REQ-UARCH-B04 | ITTAGE：间接跳转目标预测 | P1 |
| REQ-UARCH-B05 | RAS：调用/返回地址栈，含溢出处理 | P0 |
| REQ-UARCH-B06 | 循环预测器（loop predictor） | P2 |
| REQ-UARCH-B07 | 历史寄存器：全局历史 + 路径历史，支持多长度 | P1 |
| REQ-UARCH-B08 | 训练：提交级训练，误预测重定向时更新 | P0 |
| REQ-UARCH-B09 | 预测准确率目标：整数负载 ≥ 95%，MPKI ≤ 5 | P1 |

#### 6.3.5 发射与执行（Issue / Execute）

| 编号 | 需求 | 优先级 |
| --- | --- | --- |
| REQ-UARCH-E01 | 分布式发射队列，按 FU 分组；年龄矩阵选择最老就绪 uop | P0 |
| REQ-UARCH-E02 | 唤醒-选择分离（wakeup/select），支持投机唤醒与取消 | P1 |
| REQ-UARCH-E03 | 整数 ALU：加/减/逻辑/移位/比较/条件移动 | P0 |
| REQ-UARCH-E04 | 乘除法单元：64×64→128 乘法、64 位除法（多周期，可流水） | P0 |
| REQ-UARCH-E05 | 分支单元：条件解析、目标计算、误预测重定向 | P0 |
| REQ-UARCH-E06 | 地址生成单元（AGU）×2：基址+索引*比例+位移；RIP 相对 | P0 |
| REQ-UARCH-E07 | SSE 单元：标量/向量浮点与整数 SIMD，IEEE 754 | P0 |
| REQ-UARCH-E08 | 旁路网络：EX→EX、MEM→EX、WB→EX，覆盖 GPR/XMM | P0 |
| REQ-UARCH-E09 | 写回总线与 PRF 写端口仲裁，避免结构冲突 | P0 |
| REQ-UARCH-E10 | 多周期指令占用 FU 期间不阻塞其他 IQ | P1 |

#### 6.3.6 访存子系统（LSU / Cache / MMU）

| 编号 | 需求 | 优先级 |
| --- | --- | --- |
| REQ-UARCH-M01 | Load 队列/Store 队列，按程序序提交；违例检测与重放 | P0 |
| REQ-UARCH-M02 | Store→Load 转发（同地址、包含/部分重叠） | P1 |
| REQ-UARCH-M03 | 非对齐访问拆分与合并，跨 cacheline 处理 | P0 |
| REQ-UARCH-M04 | Store Buffer：16 项，写合并，提交后写 L1D | P0 |
| REQ-UARCH-M05 | 访存依赖预测（SSIT + LFST，借鉴 V3） | P1 |
| REQ-UARCH-M06 | L1 DTLB/ITLB，48/32 项，全相联或组相联 | P0 |
| REQ-UARCH-M07 | 4 级页表 walker，支持 page walk cache | P0 |
| REQ-UARCH-M08 | L1D 32KB 8 路，write-back/write-allocate，LRU/PLRU | P1 |
| REQ-UARCH-M09 | 硬件预取（stride/stream） | P2 |
| REQ-UARCH-M10 | 非临时/不可缓存访问（UC）支持 MMIO（停机口、串口） | P0 |

#### 6.3.7 提交与异常（ROB / Retire）

| 编号 | 需求 | 优先级 |
| --- | --- | --- |
| REQ-UARCH-C01 | ROB 循环队列，按序提交，支持压缩（单项多 uop） | P0 |
| REQ-UARCH-C02 | 提交时更新架构 RAT、释放物理寄存器、写回内存 | P0 |
| REQ-UARCH-C03 | 精确异常：异常指令之前的指令提交，之后的冲刷 | P0 |
| REQ-UARCH-C04 | 分支误预测：重定向并恢复重命名状态 | P0 |
| REQ-UARCH-C05 | 中断：在指令边界采样，按优先级交付 | P0 |
| REQ-UARCH-C06 | 快照恢复（RAT/FreeList checkpoint） | P1 |
| REQ-UARCH-C07 | 提交 trace 端口（pc、指令、目的寄存器、值、访存） | P0 |

### 6.4 与昆明湖 V3 的差异与裁剪

| 维度 | 昆明湖 V3 | 本项目 | 理由 |
| --- | --- | --- | --- |
| ISA | RISC-V RV64GC + RVV | x86-64 + SSE2 | 产品定位 |
| 指令长度 | 定长 32b（压缩 16b） | 变长 1–15B | 前端复杂度显著上升 |
| 译码宽度 | 6 | 4（V1.0） | 变长解码限制 |
| 向量 | RVV 128b+ | SSE2 128b | 范围裁剪 |
| 虚拟化 | 支持 H 扩展 | 不支持 | 非目标 |
| 多核/一致性 | 支持 CHI | 单核 | 非目标 |
| 缓存层次 | L1+L2+CHI | L1（+L2 P1） | 简化 |
| 分支预测 | 多级 TAGE 家族 | 同构简化版 | 先结构后精度 |
| 异常模型 | RISC-V trap/CSR | x86 IDT/CR/EFLAGS | ISA 差异 |
| 页表 | Sv39/Sv48 | 4 级 x86 页表 | ISA 差异 |

### 6.5 微架构验收

- 阶段 B 每个模块有独立单元测试（重命名映射、ROB 回绕、IQ 年龄矩阵、旁路优先级）；
- 全核 difftest 通过 10^7 条指令（含随机分支/访存序列）；
- 关键路径可综合：Yosys 无 latch、无组合环、无多驱动；
- 性能计数器可解释 IPC 损失（前端空泡、后端停顿、访存停顿、分支冲刷）。

---

## 7. 接口需求

### 7.1 顶层接口

| 编号 | 接口 | 说明 | 优先级 |
| --- | --- | --- | --- |
| REQ-IF-001 | `clk` / `reset` | 单时钟域；异步复位同步释放 | P0 |
| REQ-IF-002 | 指令存储器接口 | valid/ready，地址 64b，数据 256b（32B），支持 burst | P0 |
| REQ-IF-003 | 数据存储器接口 | valid/ready，地址 64b，数据 128b，字节掩码 16b | P0 |
| REQ-IF-004 | 中断接口 | `intr`（可屏蔽）、`nmi`、向量号 | P0 |
| REQ-IF-005 | 提交 trace | valid、pc、instr、rd、value、mem 信息 | P0 |
| REQ-IF-006 | 性能计数器 | 周期、退休数、分支、缓存缺失等 | P1 |
| REQ-IF-007 | 调试接口 | halt/step、寄存器读写（简化） | P2 |
| REQ-IF-008 | AXI4 主接口（P1） | 替代简单 valid/ready 用于 SoC 集成 | P1 |

### 7.2 存储器模型约定

- 物理地址 40 位（1TB），VA 48 位；
- 取指与数据分离端口（哈佛结构），L1 缺失由外部存储器模型响应；
- MMIO 区域（`0xF0000000`）访问必须非推测、强序；
- 存储器响应固定 1 拍延迟（仿真模型），可参数化为多拍。

### 7.3 时钟与复位

- 目标 FPGA 频率 50–100 MHz（V1.0 验证）；
- ASIC 频率目标不在 V1.0 承诺范围，综合报告给出估算；
- 复位向量 `0x400000`（可参数化），复位后 CPL=0、分页关闭、中断关闭。

---

## 8. 验证需求

### 8.1 验证策略

采用“**单元测试 + 指令级 difftest + 场景测试 + 性能回归**”四层验证：

```
┌─────────────────────────────────────────────┐
│ L4 性能/压力回归（IPC、长随机序列、边界）      │
├─────────────────────────────────────────────┤
│ L3 场景测试（分页、异常、中断、字符串、SSE2）  │
├─────────────────────────────────────────────┤
│ L2 指令级 difftest（Unicorn 参考模型）        │
├─────────────────────────────────────────────┤
│ L1 单元测试（解码器、ALU、MMU、ROB、IQ）      │
└─────────────────────────────────────────────┘
```

### 8.2 参考模型

| 编号 | 需求 | 优先级 |
| --- | --- | --- |
| REQ-VER-001 | 使用 Unicorn 2.x x86-64 作为黄金模型，装载相同 flat binary | P0 |
| REQ-VER-002 | 相同初始状态：GPR、XMM、RFLAGS、CR3、内存镜像、栈 | P0 |
| REQ-VER-003 | 逐指令比对：PC、GPR、XMM、RFLAGS、内存写（地址/数据/掩码） | P0 |
| REQ-VER-004 | 差异时输出：指令序号、PC、期望值/实际值、最近 N 条 trace、波形转储 | P0 |
| REQ-VER-005 | 对 Unicorn 不精确的语义（AF/PF、NaN、denormal）补充定向单元测试 | P0 |
| REQ-VER-006 | 支持跳过/白名单机制（如未实现指令），但必须有显式记录 | P1 |

### 8.3 测试集

| 编号 | 测试集 | 内容 | 优先级 |
| --- | --- | --- | --- |
| REQ-VER-101 | 整数指令测试 | 每条 P0 指令正例/边界例，含标志位检查 | P0 |
| REQ-VER-102 | 寻址测试 | ModRM/SIB/RIP 相对/32 位地址/非对齐 | P0 |
| REQ-VER-103 | 控制流测试 | 条件分支全组合、call/ret、间接跳转、循环 | P0 |
| REQ-VER-104 | 字符串测试 | REP 各方向、长度 0/1/大长度、跨页 | P0 |
| REQ-VER-105 | SSE2 测试 | 标量/向量四则、转换、比较、位运算、特殊值 | P0 |
| REQ-VER-106 | 异常测试 | #DE/#UD/#GP/#PF/#BP，错误码与压栈顺序 | P0 |
| REQ-VER-107 | 分页测试 | 4KB 映射、权限、未映射、写保护、TLB 失效 | P0 |
| REQ-VER-108 | 中断测试 | 外部中断、IF 屏蔽、嵌套、IRETQ | P1 |
| REQ-VER-109 | 随机指令序列 | 生成器约束在 P0 子集，含分支/访存 | P1 |
| REQ-VER-110 | 长跑测试 | 10^7 条指令 difftest | P0 |

### 8.4 覆盖率与质量门禁

| 编号 | 指标 | 目标 |
| --- | --- | --- |
| REQ-VER-201 | P0 指令覆盖 | 100% |
| REQ-VER-202 | RTL 行覆盖（Verilator coverage） | ≥ 90% |
| REQ-VER-203 | 分支/条件覆盖 | ≥ 85% |
| REQ-VER-204 | 回归通过率 | 100%（不允许已知失败） |
| REQ-VER-205 | 每次提交回归时长 | ≤ 10 分钟（小回归）；≤ 60 分钟（夜间全量） |
| REQ-VER-206 | 波形可复现 | 失败用例自动保存 seed 与 VCD/FST |

### 8.5 验证环境交付

- `tools/asm.py`：汇编→flat binary（clang/llvm-mc + objcopy）；
- `tools/difftest.py`：Unicorn 参考模型与 trace 比对；
- `sim/main.cpp`：Verilator harness（装载、运行、停机、dump）；
- `tests/`：汇编测试与期望结果；
- `make regress`：一键回归并生成报告。

---

## 9. 里程碑与验收标准

> 时间以“周”为单位，按 1 名架构/ RTL 工程师 + AI 辅助估算；实际排期以评审为准。

| 里程碑 | 周期 | 交付内容 | 退出标准（Exit Criteria） |
| --- | --- | --- | --- |
| **M0** 工具链与骨架 | W0（已完成） | sbt/Chisel/Verilator/Unicorn 环境；目录结构；EmitVerilog 流程 | `sbt compile` 通过；生成 Verilog 通过 Verilator lint |
| **M1** 解码器 | W1–W2 | 前缀扫描、opcode ROM、ModRM/SIB、立即数；Uop 定义 | 500 条指令长度/字段与 objdump 一致；非法编码正确标记 |
| **M2** 顺序整数核 | W3–W5 | 五级流水、GPR、ALU、分支、load/store、停机 MMIO；difftest | 30+ 整数测试通过；10^6 条指令 difftest 零差异 |
| **M3** SSE2 + 系统 | W6–W8 | XMM、SSE2 指令、分页、异常/IDT、SYSCALL、CPUID | 15+ SSE2 测试、10+ 异常/分页测试通过；P0 覆盖 100% |
| **M4** 乱序前端 | W9–W11 | BPU（uBTB/RAS/两级 BTB）、FTQ、4 宽译码、重命名、ROB、分派 | 重命名/ROB 单元测试通过；顺序语义 difftest 仍全绿 |
| **M5** 乱序后端 | W12–W15 | 分布式 IQ、PRF、执行单元、LSU/LSQ、SBuffer、提交与恢复 | 10^7 条指令 difftest 零差异；乱序执行下所有测试通过 |
| **M6** 定版 | W16 | 性能调优、综合检查、文档、回归报告 | IPC 目标达成；Yosys 综合通过；文档评审通过 |

### 9.1 里程碑依赖

```
M0 ──> M1 ──> M2 ──> M3 ──> M4 ──> M5 ──> M6
                │           │
                └── difftest 环境贯穿始终 ──┘
```

### 9.2 验收演示脚本

1. `make rtl` 生成 `build/rtl/X86Top.v`；
2. `make sim` 编译 Verilator 模型；
3. `make regress` 运行全部测试，输出：
   - 通过/失败统计；
   - 指令覆盖表；
   - IPC、分支 MPKI、L1 命中率；
   - 失败用例的最小复现命令。

---

## 10. 性能需求

### 10.1 指标定义

- **IPC**：退休指令数 / 周期数（含空泡）；
- **分支 MPKI**：误预测数 / 退休指令数 × 1000；
- **前端空泡率**：取指/译码停顿周期占比；
- **访存停顿**：L1 缺失与 store buffer 满导致的停顿；
- **ROB 平均占用**、**IQ 平均占用**。

### 10.2 目标

| 编号 | 指标 | 阶段 A | 阶段 B（V1.0） |
| --- | --- | --- | --- |
| REQ-PERF-001 | 整数微基准 IPC | 0.5–0.9 | ≥ 1.5（相对阶段 A ≥ 1.5×） |
| REQ-PERF-002 | 分支预测准确率 | 静态（不跳转） | ≥ 95% |
| REQ-PERF-003 | 分支 MPKI | — | ≤ 5 |
| REQ-PERF-004 | L1D 命中率（测试集） | — | ≥ 95% |
| REQ-PERF-005 | 退休宽度利用率 | 1 | ≥ 2.0 条/周期 |
| REQ-PERF-006 | CoreMark/MHz（可选） | — | 报告值，不设硬指标 |

### 10.3 基准与统计

- 微基准：`alu_mix`、`mem_stride`、`branch_mix`、`sse2_kernel`、`call_ret`；
- 统计方式：在提交级计数器中累计，回归报告输出 CSV；
- 对比基线：阶段 A 同测试集 IPC。

> 性能目标以“结构演进带来可解释提升”为准，不承诺与商业 x86 对比。

---

## 11. 非功能需求

| 编号 | 类别 | 需求 | 优先级 |
| --- | --- | --- | --- |
| REQ-NF-001 | 可综合性 | 生成的 Verilog 可被 Yosys/商业工具综合；无 latch、无组合环、无多驱动 | P0 |
| REQ-NF-002 | 可配置性 | 宽度、队列深度、缓存大小、是否含 FPU 通过 Parameters 配置 | P0 |
| REQ-NF-003 | 可调试性 | 每级 valid/ready、提交 trace、性能计数器、VCD/FST 波形 | P0 |
| REQ-NF-004 | 可复现性 | 固定工具版本；`make` 一键构建；CI 可重复 | P0 |
| REQ-NF-005 | 编码规范 | Scala 风格统一；模块注释；避免隐式宽度截断 | P0 |
| REQ-NF-006 | 文档 | 架构手册、微架构手册、验证手册、使用指南（中文） | P0 |
| REQ-NF-007 | 许可证 | 自研代码 Apache-2.0；第三方依赖兼容 | P1 |
| REQ-NF-008 | 合规 | 不复制受专利保护的微架构实现；x86 专利风险需法务评估 | P0 |
| REQ-NF-009 | 安全性 | 推测执行侧信道缓解（IBRS/STIBP 等价机制） | P2 |
| REQ-NF-010 | 可移植性 | 仿真可在 macOS/Linux 运行；不依赖特定 EDA | P0 |

---

## 12. 风险与对策

| 编号 | 风险 | 概率 | 影响 | 对策 |
| --- | --- | --- | --- | --- |
| RISK-01 | x86 变长解码复杂，边界/前缀组合错误 | 高 | 高 | 表驱动 + objdump 长度对拍 + 随机编码测试 |
| RISK-02 | SSE2/IEEE 754 语义（NaN、denormal、舍入）偏差 | 中 | 中 | 定向单元测试 + 与 Unicorn/硬件对拍；先默认舍入模式 |
| RISK-03 | Unicorn 与真实 x86 行为差异（AF/PF、未定义标志） | 中 | 中 | 关键标志用 SDM 伪代码写独立测试；差异白名单需评审 |
| RISK-04 | 乱序恢复路径（误预测/异常/中断）遗漏导致状态不一致 | 高 | 高 | 快照 + 全状态回滚测试；随机长跑 difftest |
| RISK-05 | Chisel 生成 Verilog 的仿真性能不足 | 中 | 中 | Verilator -O3 + 分区编译；必要时减少层次 |
| RISK-06 | 工具链网络受限（sbt 依赖、firtool） | 中 | 中 | 固定 Chisel 3.5.6 + SFC emitter；本地缓存依赖；记录环境 |
| RISK-07 | 范围蔓延（Linux/AVX/多核） | 高 | 高 | PRD 明确非目标；变更需评审 |
| RISK-08 | x86 专利/许可风险 | 中 | 高 | 法务评估；仅用于研究/教学；不宣称兼容认证 |
| RISK-09 | 单人力不足，里程碑延期 | 高 | 中 | 分阶段交付；M2 即可用；优先 P0 |
| RISK-10 | 性能不达预期 | 中 | 低 | 性能目标设为相对提升；用计数器定位瓶颈 |

---

## 13. 交付物清单

| 编号 | 交付物 | 路径（规划） | 里程碑 |
| --- | --- | --- | --- |
| D-01 | 本 PRD | `docs/PRD.md` | M0 |
| D-02 | 架构手册（ISA 子集、编码、异常） | `docs/architecture.md` | M3 |
| D-03 | 微架构手册（流水线、ROB、IQ、LSU） | `docs/microarchitecture.md` | M5 |
| D-04 | 验证手册（difftest、测试集、覆盖率） | `docs/verification.md` | M4 |
| D-05 | 使用指南（构建、运行、调试） | `docs/usage.md` | M2 |
| D-06 | Chisel RTL | `src/main/scala/x86/` | M2–M5 |
| D-07 | 生成的 Verilog | `build/rtl/`（构建产物） | M6 |
| D-08 | 汇编测试集 | `tests/` | M2–M3 |
| D-09 | Verilator harness 与 difftest | `sim/`、`tools/` | M2 |
| D-10 | 回归脚本与报告 | `Makefile`、`reports/` | M2 |
| D-11 | 综合/资源报告 | `reports/synth/` | M6 |
| D-12 | 性能报告 | `reports/perf/` | M6 |

---

## 14. 需求追踪矩阵

| 需求组 | 设计章节 | 实现模块（规划） | 验证方法 | 里程碑 |
| --- | --- | --- | --- | --- |
| REQ-ISA-0xx 模式/寄存器 | §5.2 | `core/RegFile`、`core/CSR` | 单元测试 + difftest | M2–M3 |
| REQ-ISA-1xx 传送 | §5.3.1 | `frontend/Decoder`、`backend/ALU` | 指令测试 | M2 |
| REQ-ISA-2xx 运算 | §5.3.2 | `backend/ALU`、`backend/MulDiv` | 指令测试 + 标志位 | M2 |
| REQ-ISA-3xx 控制流 | §5.3.3 | `backend/BranchUnit`、`frontend/BPU` | 控制流测试 | M2/M4 |
| REQ-ISA-4xx 字符串 | §5.3.4 | `backend/StringUnit` | 定向测试 | M3 |
| REQ-ISA-5xx 系统 | §5.3.5 | `core/CSR`、`mem/MMU`、`core/Trap` | 异常/中断测试 | M3 |
| REQ-ISA-6xx SSE2 | §5.3.6 | `backend/SSEUnit`、`backend/XmmRegFile` | SSE2 测试 | M3 |
| REQ-ENC-0xx 编码 | §5.4 | `frontend/Decoder` | objdump 对拍 | M1 |
| REQ-UARCH-F0x 前端 | §6.3.2 | `frontend/Fetch`、`FTQ`、`BPU` | 单元 + 性能 | M4 |
| REQ-UARCH-R0x 重命名 | §6.3.3 | `backend/Rename` | 单元测试 | M4 |
| REQ-UARCH-E0x 执行 | §6.3.5 | `backend/IssueQueue`、`Exu` | 单元 + difftest | M4–M5 |
| REQ-UARCH-M0x 访存 | §6.3.6 | `mem/LSU`、`mem/MMU`、`mem/Cache` | 场景测试 | M5 |
| REQ-UARCH-C0x 提交 | §6.3.7 | `backend/ROB` | 快照/异常测试 | M4–M5 |
| REQ-VER-0xx 验证 | §8 | `tools/`、`sim/` | 回归报告 | M2–M6 |
| REQ-NF-0xx 非功能 | §11 | 全局 | 综合/评审 | M6 |

---

## 15. 附录

### 15.1 x86-64 编码速查

**前缀（顺序无关，REX 必须最后）**

| 字节 | 含义 |
| --- | --- |
| 0x66 | 操作数大小覆盖（32→16） |
| 0x67 | 地址大小覆盖（64→32） |
| 0xF0 | LOCK |
| 0xF2 / 0xF3 | REPNE / REP（SSE 中作为 mandatory prefix） |
| 0x2E/0x36/0x3E/0x26/0x64/0x65 | CS/SS/DS/ES/FS/GS 段覆盖 |

**REX（0x40–0x4F）**

```
7 6 5 4 3 2 1 0
0 1 0 0 W R X B
W: 64 位操作数
R: ModRM.reg 扩展
X: SIB.index 扩展
B: ModRM.rm / opcode reg 扩展
```

**ModRM / SIB**

```
ModRM: [mod:2][reg:3][rm:3]
SIB:   [scale:2][index:3][base:3]
scale: 0→1, 1→2, 2→4, 3→8

mod=00, rm=101        → RIP + disp32
mod=00, rm=100, SIB   → base/index 规则；base=101 → disp32 无基址
mod=01                → disp8
mod=10                → disp32
index=100 且 REX.X=0  → 无索引
```

**长度上限**：15 字节；超长 → #GP。

### 15.2 异常与错误码

| 向量 | 名称 | 错误码 | 触发条件 |
| --- | --- | --- | --- |
| 0 | #DE | 无 | 除零、商溢出 |
| 3 | #BP | 无 | INT3 |
| 6 | #UD | 无 | 非法/未实现编码 |
| 8 | #DF | 有（0） | 异常处理期间再次异常 |
| 13 | #GP | 有 | 非规范地址、权限、非法段/指令 |
| 14 | #PF | 有 | 页表缺失/权限/保留位 |
| 17 | #AC | 有 | 对齐检查（P2） |

**#PF 错误码**：bit0 P（保护违规）、bit1 W（写）、bit2 U（用户）、bit3 RSVD、bit4 I（取指）。

### 15.3 分页（4 级，48 位 VA）

```
VA[47:39] → PML4E
VA[38:30] → PDPTE
VA[29:21] → PDE
VA[20:12] → PTE
VA[11:0]  → 页内偏移
```

- CR3 指向 PML4（物理地址，4KB 对齐）；
- 页表项 64 位：P/RW/US/PWT/PCD/A/D/PS/PAT/G/物理地址/NX；
- 非规范地址（bit47 ≠ bits63:48）访问 → #GP。

### 15.4 关键 MSR / CR

| 名称 | 地址/编号 | 用途 |
| --- | --- | --- |
| EFER | MSR 0xC0000080 | SCE/NXE/LME/LMA |
| STAR | MSR 0xC0000081 | SYSCALL/SYSRET 段选择子 |
| LSTAR | MSR 0xC0000082 | SYSCALL 入口 RIP |
| SFMASK | MSR 0xC0000084 | SYSCALL 屏蔽的 RFLAGS 位 |
| CR0 | — | PE/PG/WP 等 |
| CR2 | — | 页错误线性地址 |
| CR3 | — | 页表基址 |
| CR4 | — | PAE/PGE 等 |

### 15.5 微架构参数汇总（V1.0）

| 参数 | 值 |
| --- | --- |
| 取指 | 32B/周期 |
| 译码 | 4 条/周期 |
| 重命名/分派 | 4 / 6 uop |
| ROB | 128 项 |
| 退休 | 4 项/周期 |
| 整数 PRF | 160 |
| XMM PRF | 128 |
| IQ | 2×ALU 16、MUL 12、2×LSU 16、BR 12 |
| LQ/SQ | 48 / 32 |
| L1I/L1D | 32KB 8 路 / 32KB 8 路 |
| TLB | ITLB 32、DTLB 48、L2 TLB 1024 |
| 分支预测 | uBTB + 两级 BTB + TAGE-SC + ITTAGE + RAS |

### 15.6 待决问题（Open Questions）

| 编号 | 问题 | 建议 | 决策人 |
| --- | --- | --- | --- |
| Q-1 | 首版是否需要 2MB 大页？ | 建议 V1.1，首版仅 4KB | 架构组 |
| Q-2 | 是否需要 uop cache？ | 建议 V1.2，先保证正确性 | 架构组 |
| Q-3 | 目标工艺/频率？ | V1.0 仅功能与 FPGA 验证 | 项目组 |
| Q-4 | 是否启动 Linux？ | 单独立项，V1.0 不承诺 | 项目组 |
| Q-5 | 许可证选择？ | Apache-2.0 | 法务/项目组 |
| Q-6 | 阶段 B 译码宽度 4 还是 6？ | 先 4，uop cache 后评估 6 | 架构组 |

---

### 15.7 与昆明湖 V3 的术语对照

| 昆明湖 V3 | 本项目 | 说明 |
| --- | --- | --- |
| FTQ | FTQ | 取指目标队列 |
| IFU | IFU | 取指单元 |
| ICache | L1I | 指令缓存 |
| DecodeUnit | Decoder | 译码 |
| Rename | Rename | 重命名 |
| Dispatch | Dispatch | 分派 |
| Rob | ROB | 重排序缓冲 |
| IssueQueue | IQ | 发射队列 |
| LoadUnit/StoreUnit | LSU | 访存单元 |
| SBuffer | SBuffer | 存储提交缓冲 |
| L1TLB/L2TLB | TLB/PTW | 地址翻译 |
| CoupledL2 | L2 | 二级缓存（P1） |
| RAB | RAB | 重命名缓冲（P1） |

---

*文档结束。评审通过后作为设计与验收基线；变更需记录修订历史并更新需求追踪矩阵。*
