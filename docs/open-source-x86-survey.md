# 开源 x86 处理器 / 芯片调研

> 调研日期：2026-11-03  
> 范围：GitHub、OpenCores / LibreCores、项目 README 与作者博客  
> 说明：Star 数为当日 GitHub API 快照，仅作热度参考；许可证以各仓库 `LICENSE` 为准。本文不是法律意见。

## 0. 结论摘要

| 问题 | 结论 |
| --- | --- |
| 有没有开源的 x86 CPU RTL？ | **有**，覆盖 8086 / 80186 / 80286 / 80386 / 80486 |
| 有没有可流片、可量产的开源 x86 物理芯片？ | **截至调研时间未查到**公开、可复现的 open-source x86 ASIC / 流片项目 |
| 有没有开源的 x86-64 硬件核？ | **未查到成熟实现**；GitHub 上只有教学 / 实验性质的项目 |
| 有没有能跑完整系统的开源 x86 软核？ | **有**：ao486（DOS / Windows 95 / Linux）、PCXT（PC/XT）、486tang 等 |
| 有没有开源的 x86 模拟器？ | **有**：QEMU、Bochs、Unicorn、v86、Box64、FEX-Emu 等 |
| 为什么没有 RISC-V 那样的生态？ | ISA 不开放 + 专利 / 授权 + 微码版权 + 验证成本 + 商业垄断 |

一句话：**“开源 x86 芯片”目前只在 FPGA 软核 / RTL 层面成立，而且成熟度止步于 486；x86-64 是公开资料中的空白。**

---

## 1. 先厘清概念：你说的“芯片”是哪一种？

| 层次 | 含义 | 是否存在开源 x86 | 代表 |
| --- | --- | --- | --- |
| **物理芯片 / ASIC** | 有掩膜、有封装、可购买或可流片的硅片 | 未查到公开项目 | 无 |
| **可综合 RTL / 软核** | Verilog/SystemVerilog，可下载、可仿真、可上 FPGA | 有，且不止一个 | ao486、z386、z486、80x86、Zet |
| **FPGA SoC / 整机** | CPU + 外设 + 视频 + 存储，能启动 DOS/Windows/Linux | 有 | ao486_MiSTer、PCXT、486tang |
| **模拟器 / 虚拟化** | 用软件解释或动态翻译 x86 指令 | 有，生态成熟 | QEMU、Bochs、Unicorn、v86 |
| **开源主板** | 板子开源，但 CPU 是买来的商用芯片 | 有，但**不是开源 CPU** | M8SBC-486、Xeraster 486 主板 |

> 很多标题里的“开源 486”其实是“开源 486 主板 + 商用 486 CPU”，和“开源 CPU”是两件事。

---

## 2. 项目地图

### 2.1 能跑完整系统的开源 x86 SoC

| 项目 | ISA / 级别 | 实现 | 许可证 | Star | 状态 / 备注 |
| --- | --- | --- | --- | ---: | --- |
| [ao486](https://github.com/alfikpl/ao486) | 486SX | Verilog，Altera Cyclone IV | BSD-3（RTL）/ LGPL（Bochs 部分） | 477 | 2014 年原版；SoC 含 IDE、软驱、PIC、DMA、SB 2.0、PIT、PS/2、RTC、VGA；README 称可启动 Linux 3.13 与 Windows 95 |
| [ao486_MiSTer](https://github.com/MiSTer-devel/ao486_MiSTer) | 486SX | Verilog，MiSTer / DE10-Nano | BSD-3 / LGPL | 374 | 活跃维护（2026-10），MiSTer 上最常用的 486 核心 |
| [z486_MiSTer](https://github.com/nand2mario/z486_MiSTer) | 486-class | SystemVerilog，MiSTer | 仓库未声明 | 124 | z486 的 MiSTer 整机；2026-10 仍有 release |
| [486tang](https://github.com/nand2mario/486tang) | 486SX | Verilog，Sipeed Tang Console 138K | BSD-3 | 128 | ao486 的 Tang 移植；约 486SX-20，DOS 6.22 可用 |
| [PCXT](https://github.com/spark2k06/PCXT_MiSTer) | 8088 / 8086 | Verilog/SystemVerilog，MiSTer | GPL-3.0 | 64 | 完整 IBM PC/XT：MCL86 + KFPC-XT，CGA/Hercules、AdLib；4.77–9.54 MHz |
| [z486_XL](https://github.com/nand2mario/z486_XL) | 486-class + Voodoo 1 | SystemVerilog，Xilinx KV260 | Apache-2.0 | 16 | 带 Voodoo 1 3D 的 FPGA PC |

### 2.2 CPU 核 / 研究项目

| 项目 | ISA / 级别 | 实现 | 许可证 | Star | 状态 / 备注 |
| --- | --- | --- | --- | ---: | --- |
| [z386](https://github.com/nand2mario/z386) | 80386 | SystemVerilog | Apache-2.0（`ucode.hex` 除外） | 192 | 2026-05 发布；微码驱动，含预取/译码、分段、分页、保护、I/D cache；15,545 ALMs，0.225 DMIPS/MHz |
| [z486](https://github.com/nand2mario/z486) | 80486-class | SystemVerilog | Apache-2.0（`ucode.hex` 除外） | 31 | 2026-08 发布；D1/D2 流水前端、硬布线 + 微码、实验性 x87；21,906 ALMs（含 FPU），0.330 DMIPS/MHz；Doom 29.1 FPS @ 85 MHz |
| [80x86 / S80186](https://github.com/jamieiles/80x86) | 80186 | SystemVerilog | GPL-3.0 | 422 | 3 级流水，60 MHz，约 1,800 ALMs；带 C++ 参考模型、测试、BIOS、DE0-Nano/DE0-CV 参考设计 |
| [Zet / zet86](https://github.com/marmolejo/zet) | 8086 级 SoC | Verilog | GPL-3.0 | 345 | 老牌项目；DE0/DE1/DE2，BIOS，可跑 MS-DOS/FreeDOS；2018 后基本停更 |
| [SPK286](https://github.com/spark2k06/SPK286) | 80286 | SystemVerilog | MIT | 7 | 2026 新项目；自有微码 + 微码编译器；实模式/保护模式、LOADALL、任务切换；用 SingleStepTests/80286 验证 |
| [z8086](https://github.com/nand2mario/z8086) | 8086 / 8088 | SystemVerilog | **无许可证** | 114 | 执行原始 8086 微码；16,150/16,150 单步向量通过；约 1,200 ALMs；无 LOCK、无协处理器接口 |
| [KFPC-XT](https://github.com/kitune-san/KFPC-XT) | XT chipset | SystemVerilog | MIT | 14 | PC/XT 芯片组，常与 MCL86 搭配 |
| [MCL86](https://github.com/MicroCoreLabs/Projects/tree/master/MCL86) | 8088 | Verilog | **无许可证** | 534（整个 Projects 仓库） | Ted Fried 的 8088 核；PCXT 在用；严格说是 source-available，不是 OSI 开源 |
| [w8086](https://github.com/openx86/w8086) | 8086 | SystemVerilog | Apache-2.0 | 6 | 早期项目，README 仅一行 |
| [openx86/openx86](https://github.com/openx86/openx86) | — | — | Apache-2.0 | 14 | 组织级尝试，仓库基本为空 |

### 2.3 教学 / 实验 / 早期项目

| 项目 | 内容 | 许可证 | Star | 说明 |
| --- | --- | --- | ---: | --- |
| [yuhao/x86-core](https://github.com/yuhao/x86-core) | 门级 Verilog x86 核 | 无 | 7 | 2013 年，规模很小 |
| [MrTejas/y86-64-bit-processor](https://github.com/MrTejas/y86-64-bit-processor) | x86-64 顺序 / 5 级流水 | 无 | 14 | 课程项目，非完整系统 |
| [wedddy0707/CPUEX_x86-64](https://github.com/wedddy0707/CPUEX_x86-64) | x86-64 练习核 | 无 | 2 | 2020 年，未成熟 |
| [SavourySnaX/8088_Core](https://github.com/SavourySnaX/8088_Core) | 8088 核 | 无 | 2 | 用于 MiSTer Konix Multisystem |
| [openx86/w8086](https://github.com/openx86/w8086) | 8086 核 | Apache-2.0 | 6 | 早期 |

> 这些项目可以看思路，但不要当作可复用 IP：多数没有许可证、没有验证、没有完整系统。

### 2.4 “开源 486 主板”不是“开源 CPU”

| 项目 | 说明 |
| --- | --- |
| [M8SBC-486](https://github.com/maniekx86/M8SBC-486) | 开源 486 主板（原理图/PCB/BIOS/VHDL），CPU 是真实的 486 芯片 |
| [Xeraster/32bit_486_motherboard](https://github.com/Xeraster/32bit_486_motherboard) | DIY 486 主板，芯片组用 FPGA，CPU 仍是商用 486 |
| [Xeraster/dual-ice40-fpga-486-chipset](https://github.com/Xeraster/dual-ice40-fpga-486-chipset) | 用 ice40 做的 486 芯片组，不是 CPU |

---

## 3. 重点项目详解

### 3.1 ao486：最成熟的 486 开源核

- 作者 Aleksander Osman，2014 年发布，Verilog。
- 实现 486SX 全部指令（含 CPUID），4 级流水（decode / read / execute / write），16 KB 指令 cache + 16 KB write-back 数据 cache，32 项 TLB。
- SoC 集成 IDE、软驱、8259 PIC、8237 DMA、Sound Blaster 2.0（DSP + 部分 OPL2）、8254 PIT、8042 键盘鼠标、RTC、标准 VGA。
- 最初跑在 Terasic DE2-115（Cyclone IV E），后来被移植为 MiSTer 的 `ao486_MiSTer`，是 MiSTer 上最成熟的 486 核心。
- 许可证：`rtl/`、`ao486_tool/`、`sim/` 为 BSD-3；`bochs486/`、`bochsDevs/` 来自 Bochs，LGPL。
- 局限：**486SX，无 FPU；32 位；没有 x86-64**；原仓库 2014 年后基本停更，活跃的是 MiSTer 移植。

### 3.2 z386 / z486：最新的 386/486 开源核

**z386**（2026-05）

- 80386 兼容，SystemVerilog；不是逐条指令硬写，而是围绕原始 Intel 386 微码重构硬件控制。
- 含预取/译码、微码序列器、分段、分页、保护检查、整数 ALU/移位、总线、可配置 I/D cache。
- 资源：15,545 ALMs（Cyclone V），0.225 DMIPS/MHz。
- 许可证：Apache-2.0，但 **`ucode.hex` 被明确排除在授权之外**（作者未把微码纳入 Apache 授权）。
- 适合：学习 32 位 x86 的微码组织、保护模式、分页、验证方法。

**z486**（2026-08）

- 80486-class，SystemVerilog；D1/D2 流水前端，常见指令硬布线，复杂指令走微码；集成实验性 x87（能跑 Quake 的子集）。
- 资源：21,906 ALMs（含 x87），完整 PC 35,686 ALMs（DE10-Nano 41,910 ALMs）。
- 性能：0.330 DMIPS/MHz；Doom 29.1 FPS @ 85 MHz（ao486 21.0 FPS @ 90 MHz）；Quake 约 6.3 FPS。
- 2026-10 版本进一步优化，Dhrystone CPI 从 2.8 降到约 2.0。
- 许可证：Apache-2.0，同样排除 `ucode.hex`。
- 适合：学习变长 x86 的 D1/D2 前端、硬布线/微码混合、x87 子集、FPGA 面积/时序权衡。

### 3.3 80x86 / S80186：验证最完整的 16 位核

- 80186 二进制兼容，SystemVerilog，3 级 fetch/decode/execute 流水。
- 60 MHz（Cyclone V），约 1,800 ALMs。
- 附带：C++ 参考模型、大量测试、参考 BIOS、SDRAM/SD/VGA/PS2 参考设计、Docker 构建。
- 可跑 MS-DOS / FreeDOS，OpenWatcom/GCC ia16-elf 编译的程序。
- 许可证 GPL-3.0。
- 价值：**验证方法**（RTL vs C++ 模型 vs FPGA 三层对拍）和**参考系统**，比 CPU 本身更值得借鉴。

### 3.4 Zet：老牌 8086 SoC

- 8086 级，Verilog，GPL-3.0；DE0/DE1/DE2/DE2-115，带 BIOS，可跑 MS-DOS/FreeDOS。
- 2014 年前后活跃，2018 后停更；工具链老（Quartus 10）。
- 价值：完整的 8086 SoC + BIOS + 软件栈，适合理解“最小 x86 系统”。

### 3.5 SPK286：2026 年的 80286 核

- SystemVerilog，MIT；自有微码和微码编译器。
- 实模式全部指令 + 保护模式（描述符、特权检查、调用/中断/任务门、任务切换、IOPL、异常、双重故障、关机）。
- 支持 LOADALL（0F 05）和 SALC（D6）等未公开指令。
- 用 SingleStepTests/80286 硬件捕获数据验证；无协处理器接口（无 NPX）。
- 价值：**微码编译器 + 硬件捕获测试**的方法，很适合借鉴到自己的验证流程。

### 3.6 z8086：用原始微码的 8086

- 执行原始 8086 微码（21 位微指令，`ucode.hex`）。
- 16,150/16,150 单步向量通过；约 1,200 ALMs（Cyclone V）、1,800 LUTs（Artix-7）。
- 已在 Gowin GW5A、Artix-7、Cyclone V 上综合验证。
- **无许可证**：只能阅读，不能直接复用。
- 价值：微码引擎、位切片 ALU、FPGA 友好接口的极简参考。

### 3.7 PCXT / MCL86：完整 IBM PC/XT

- PCXT 是 MiSTer 上的 IBM PC/XT 整机：MCL86（8088）+ KFPC-XT 芯片组 + CGA/Hercules + AdLib + 软盘/硬盘。
- CPU 频率 4.77 / 7.16 / 9.54 MHz / Max；可选 8088 或 8086 总线模式。
- PCXT 是 GPL-3.0，但 **MCL86 本身没有许可证**；两者许可证状态要分开看。
- 价值：完整系统集成、总线时序、外设模型；CPU 本身不建议直接复用。

### 3.8 x86-64 教学项目

- [MrTejas/y86-64-bit-processor](https://github.com/MrTejas/y86-64-bit-processor)：Verilog，顺序 + 5 级流水，处理 load-use、跳转预测等；课程项目。
- [wedddy0707/CPUEX_x86-64](https://github.com/wedddy0707/CPUEX_x86-64)：练习核，未成熟。
- 共同点：**只覆盖指令子集；未见完整长模式、4 级页表、SSE2、可运行系统或系统级验证**。
- 结论：x86-64 的“开源硬件”目前只有教学价值，不能作为工程基线。

---

## 4. 为什么 x86-64 是空白？

### 4.1 技术复杂度

- **变长解码**：1–15 字节，前缀、REX、ModRM/SIB、disp/imm 组合；64 位又增加 REX.W、RIP-relative、imm64。
- **长模式**：4 级页表、EFER、SYSCALL/SYSRET、规范地址检查、兼容模式（即使只做 long mode，也要处理 32 位操作数/地址）。
- **SSE2**：128 位寄存器、IEEE 754、NaN/denormal/舍入、MXCSR；上述项目中未见完整 SSE2 实现。
- **乱序**：x86 的变长前端 + 复杂异常/内存模型，使 ROB/重命名/LSU 的验证难度远高于 RISC-V。
- **验证空间**：x86-64 指令编码组合、标志位、异常优先级、页表权限、自修改代码……组合爆炸。

### 4.2 法律与授权

- x86 ISA 是 Intel/AMD 的私有指令集；实现者通常需要专利交叉授权或 clean-room 设计。
- 8086/80286/80386/80486 的核心专利已过期（20 年保护期），所以 486 级 clean-room 实现可行；这也是 486 级实现能公开存在的重要原因。
- x86-64（AMD64）及相关扩展的专利/授权更复杂；SSE2、虚拟化等还有大量后续专利。
- **微码版权**：Intel 微码不是自由软件；z386/z486 明确把 `ucode.hex` 排除在 Apache 授权外，就是信号。
- **商标**：不能宣称“Intel 兼容”“AMD64”等；通常写“x86-64 compatible”。

### 4.3 商业与生态

- x86 由 Intel/AMD 主导，VIA/Zhaoxin、Hygon 通过授权进入；没有厂商有动力开放高性能 RTL。
- RISC-V 有开放 ISA、开放核（香山、Rocket、BOOM、CVA6）、开放 PDK（SkyWater/GF180）和流片；x86 没有对应生态。
- 开源 x86 的需求主要在：复古计算/FPGA 游戏机、教学、老系统维护、验证研究——都不需要 x86-64 高性能核。

### 4.4 结果

- **16/32 位**：有多个可跑系统的开源软核。
- **x86-64**：没有成熟 RTL；工程上要么用 QEMU/Bochs/Unicorn 模拟，要么用商用核。
- **ASIC**：未查到公开的 open-source x86 流片；即使有 RTL，流片还涉及 PDK、IP、专利、验证和资金。

---

## 5. 许可证与法律注意事项

| 项目 | 许可证 | 注意 |
| --- | --- | --- |
| ao486 | BSD-3（RTL）/ LGPL（Bochs 部分） | BSD 部分可商用；Bochs 部分需遵守 LGPL |
| z386 / z486 | Apache-2.0，但排除 `ucode.hex` | 不能把 Intel 微码当作 Apache 授权内容 |
| 80x86 | GPL-3.0 | 衍生作品需 GPL；商用需注意 |
| Zet | GPL-3.0 | 同上 |
| PCXT | GPL-3.0 | 但 MCL86 无许可证，不能视为 GPL |
| 486tang | BSD-3 | 可复用性较好 |
| z486_XL | Apache-2.0 | 可复用性较好 |
| SPK286 | MIT | 最宽松，适合参考 |
| KFPC-XT | MIT | 芯片组可复用 |
| z8086 | 无许可证 | 只能阅读，不能复制/分发 |
| MCL86 | 无许可证 | source-available，不是 open source |
| z486_MiSTer | 未声明 | 复用前联系作者确认 |

**通用建议**：

1. 不要把“代码公开”当成“开源”；没有 LICENSE 的仓库默认保留所有权利。
2. 不要复制 Intel/AMD 的微码、BIOS、专利实现；clean-room + 公开文档更安全。
3. 使用 GPL 代码会传染；如果目标是 Apache-2.0/MIT 的自研项目，避免直接链接 GPL RTL。
4. 对外表述用“x86-64 compatible”，不要用 Intel/AMD 商标；是否可称“兼容”需法务评估。
5. 流片前做 FTO（自由实施）检索；专利过期只适用于老一代。

---

## 6. 对 OpenX86 项目的启示

结合本仓库 [`PRD.md`](PRD.md)（x86-64 长模式 + SSE2、Chisel、乱序、Unicorn difftest、对标香山昆明湖 V3）：

### 6.1 定位：公开资料中的空白

- 上述开源 x86 核全部是 **16/32 位**，没有 x86-64，更没有乱序 x86-64。
- 上述开源 x86 核中 **未见完整 SSE2 实现**；z486 只有实验性 x87 子集。
- 因此 OpenX86 如果完成，会是公开资料中少见的 x86-64 乱序核；但“首个”表述要谨慎，可能有未公开的学术/工业项目。

### 6.2 可以直接借鉴的

| 项目 | 借鉴点 | 不能直接用的原因 |
| --- | --- | --- |
| z386 / z486 | 变长前端 D1/D2、硬布线 + 微码混合、保护模式/分页、单步测试框架 | 32 位；`ucode.hex` 不在 Apache 授权内 |
| 80x86 | C++ 参考模型、RTL/模型/FPGA 三层对拍、BIOS、FPGA 参考设计 | 80186，16 位；GPL-3.0 |
| SPK286 | 微码编译器、SingleStepTests 硬件捕获验证方法 | 80286，16 位；MIT 可参考 |
| z8086 | 微码引擎、位切片 ALU、极简总线 | 8086，16 位；无许可证 |
| ao486 | 完整 SoC/外设、Win95/Linux 启动、系统集成 | 486SX，32 位；BSD 可参考 |
| Zet | 最小 8086 系统 + BIOS + 软件栈 | 8086；GPL-3.0；工具链老 |

### 6.3 验证：x86-64 没有现成的“SingleStepTests”

- [SingleStepTests](https://github.com/SingleStepTests) 目前有 8086、80186、80286、80386、8088 等硬件捕获测试，**没有 x86-64**。
- [test386.asm](https://github.com/barotto/test386.asm)（GPL-3.0）覆盖 386 保护模式，可作为 32 位子集回归。
- x86-64 需要：
  - 自建单步/随机指令测试生成器；
  - Unicorn/QEMU 作为 golden model，但注意 AF/PF、NaN、denormal、未定义标志、指令边界等差异；
  - 真实硬件 trace / Intel SDE / Bochs 交叉验证；
  - 异常/页表/中断的定向测试。
- 你们的 PRD 已经用 Unicorn 做 difftest，这是合理起点；建议尽早补“指令编码长度/边界”与“标志位”两类定向测试。

### 6.4 微架构：香山方法论可以借，RTL 不能搬

- 香山是 RISC-V，BPU/ROB/重命名/PRF/发射队列的**方法论**可以借鉴，但 RTL 不能直接用于 x86。
- x86 前端是最大差异：变长解码、指令边界、uop cache、分支目标可能落在指令中间。
- z486 的 D1/D2 是 32 位变长前端的现成参考；64 位还要加 REX、REX.W、RIP-relative、imm64、4 级页表。
- 后端（ROB/重命名/LSU）可以按香山结构做，但 x86 的异常/内存模型/标志位会显著增加验证难度。

### 6.5 建议的差异化

1. **不要做“另一个 486”**：ao486/z486 已经覆盖 32 位复古市场，且它们不解决 x86-64。
2. **做“64 位长模式 + 乱序 + Chisel + 可复现 + difftest”**：这是公开资料的空白。
3. **先顺序核跑通 M2/M3，再 OoO**：PRD 的分阶段路线是对的；x86-64 的前端/SSE2/异常已经足够难。
4. **把验证当一等公民**：x86-64 没有现成单步集，验证基础设施本身就是贡献。
5. **许可证策略**：自研 Apache-2.0；参考 GPL 项目时只学方法，不复制代码；不碰 Intel 微码。

### 6.6 风险提醒

- **变长解码**：M1 解码器是最大风险；建议用 objdump/llvm-mc 做长度/字段对拍，再加随机编码测试。
- **SSE2 语义**：NaN/denormal/舍入/MXCSR 容易错；先默认 round-to-nearest-even，再做定向测试。
- **乱序恢复**：误预测/异常/中断的快照恢复是高风险区；用随机长跑 difftest。
- **Unicorn 偏差**：Unicorn 是 QEMU 派生，不是硬件黄金模型；关键语义要独立测试。
- **法律**：x86-64 专利/许可风险高于 486；建议早做法务评估，对外定位“研究/教学”。
- **范围蔓延**：Linux bring-up 约为当前范围 2–3 倍工作量（PRD 已说明）；AVX、多核更大。PRD 的非目标要守住。

---

## 7. 参考链接

### 项目

- ao486：<https://github.com/alfikpl/ao486>
- ao486_MiSTer：<https://github.com/MiSTer-devel/ao486_MiSTer>
- z386：<https://github.com/nand2mario/z386>
- z486：<https://github.com/nand2mario/z486>
- z486_MiSTer：<https://github.com/nand2mario/z486_MiSTer>
- 486tang：<https://github.com/nand2mario/486tang>
- z486_XL：<https://github.com/nand2mario/z486_XL>
- 80x86：<https://github.com/jamieiles/80x86>
- Zet：<https://github.com/marmolejo/zet>
- SPK286：<https://github.com/spark2k06/SPK286>
- z8086：<https://github.com/nand2mario/z8086>
- PCXT：<https://github.com/spark2k06/PCXT_MiSTer>
- KFPC-XT：<https://github.com/kitune-san/KFPC-XT>
- MCL86：<https://github.com/MicroCoreLabs/Projects/tree/master/MCL86>
- w8086：<https://github.com/openx86/w8086>

### 文章 / 文档

- z486 技术报告：<https://nand2mario.github.io/posts/2026/z486/>
- z386 介绍：<https://nand2mario.github.io/posts/2026/z386/>
- 80386 微码反汇编：<https://www.reenigne.org/blog/80386-microcode-disassembled/>
- Intel SDM：<https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html>
- AMD64 APM：<https://www.amd.com/en/developer/resources/technical-documentation/amd64-architecture-programmers-manual.html>

### 测试

- SingleStepTests：<https://github.com/SingleStepTests>
- test386.asm：<https://github.com/barotto/test386.asm>
- ao486 x86 validation suite：<https://github.com/alfishe/ao486-x86-validation-suite>

---

## 附录 A：快速上手

| 目标 | 推荐 |
| --- | --- |
| 玩 486 复古 PC | MiSTer + ao486_MiSTer，或 486tang |
| 玩 PC/XT | MiSTer + PCXT |
| 读 386 微架构 | z386（先看 `z386.sv`、`decoder.sv`、`paging_unit.sv`、`protection.sv`） |
| 读 486 微架构 | z486（先看 `z486.sv`、`hardwired_control.sv`、`microsequencer.sv`、`x87/`） |
| 学验证方法 | 80x86（C++ 模型 + tests）、SPK286（SingleStepTests） |
| 学 8086 微码 | z8086（`src/z8086/z8086.sv`、`ucode.hex`）、Zet |
| 研究 x86-64 | 无现成 RTL；用 Intel SDM + AMD APM + Unicorn/QEMU + test386 + 自建测试 |

## 附录 B：阅读顺序建议（给 OpenX86）

1. `z486` 的 D1/D2 前端与 `hardwired_control.sv`：理解 32 位变长 x86 的流水组织。
2. `z386` 的 `decoder.sv` / `ucode.hex` / `paging_unit.sv`：理解微码 + 保护模式 + 分页。
3. `80x86` 的 `rtl/` 与 `tests/`：理解 RTL/模型/FPGA 三层验证。
4. `SPK286` 的 `docs/SPK286.md` 与微码编译器：理解硬件捕获测试。
5. `ao486` 的 SoC/外设：理解完整 PC 系统集成。
6. Intel SDM Vol.2/3 + AMD APM Vol.2/3：x86-64 长模式、页表、异常、SYSCALL。
7. 香山文档：只借方法论（BPU/ROB/重命名/发射队列/difftest），不要搬 RTL。

---

*本文为调研记录，不构成法律意见；许可证与专利状态请以官方文本和专业法务意见为准。*
