# OpenX86 文档索引

本目录存放 OpenX86 处理器项目的设计与验证文档。

This directory contains the design and verification documents for OpenX86.
The documents are primarily written in Chinese; the table below is the English index.

| Document | Status | Description |
| --- | --- | --- |
| [PRD.md](PRD.md) | draft v0.1 | Product requirements: scope, ISA subset, microarchitecture (vs. Kunminghu V3), verification, milestones, risks |
| [stage-a.md](stage-a.md) | implemented | In-order core structure, ISA coverage, verification results and limitations |
| [open-source-x86-survey.md](open-source-x86-survey.md) | 2026-11-03 | Survey of open-source x86 processors/chips: projects, licenses, x86-64 gap, lessons |
| [x86-companies.md](x86-companies.md) | 2026-11-03 | Map of x86 chip companies: Intel/AMD, VIA/Zhaoxin/Hygon, DM&P/RDC, Montage, history and market |
| architecture.md | planned | Architecture manual: encoding, registers, exceptions, paging, instruction semantics |
| [microarchitecture.md](microarchitecture.md) | v3.8 | Microarchitecture manual: Stage-A, Stage-B, comparison with Kunminghu V3, verification and known issues |
| [verification.md](verification.md) | v3.8 | Verification manual: test matrix, coverage, performance, limitations, reproduce commands, gap to V3 |
| [emu_testplan.md](emu_testplan.md) | v1 | emu per-instruction test plan: layers, fuzzing, undefined-behavior policy, coverage, defects |
| [xemu-plan.md](xemu-plan.md) | v1 | XEMU plan: self-written x86-64 reference model, milestones, interfaces, risks, acceptance |
| usage.md | planned | Build, simulation and debugging guide |

中文索引（同下）：

| 文档 | 状态 | 说明 |
| --- | --- | --- |
| [PRD.md](PRD.md) | v0.1 草稿 | 产品需求文档：范围、ISA 子集、微架构（对标昆明湖 V3）、验证、里程碑、风险 |
| [stage-a.md](stage-a.md) | 已实现 | 顺序核结构、指令覆盖、验证结果与已知限制 |
| [open-source-x86-survey.md](open-source-x86-survey.md) | 2026-11-03 | 开源 x86 处理器/芯片调研：项目清单、许可证、x86-64 空白、对本项目的启示 |
| [x86-companies.md](x86-companies.md) | 2026-11-03 | 全球 x86 芯片公司地图：Intel/AMD、VIA/兆芯/海光、DM&P/RDC、澜起津逮、历史厂商与市场格局 |
| architecture.md | 规划中 | 架构手册：编码、寄存器、异常、分页、指令语义 |
| [microarchitecture.md](microarchitecture.md) | v3.8 | 微架构手册：Stage-A 顺序流水线、Stage-B 乱序核、与昆明湖 V3 对照、验证与已知问题 |
| [verification.md](verification.md) | v3.8 | 验证手册：测试矩阵、覆盖范围、性能观察、已知限制、复现命令、与 V3 的差距与路线 |
| [emu_testplan.md](emu_testplan.md) | v1 | emu 逐指令测试计划：分层、随机 fuzz、未定义行为策略、覆盖矩阵、缺陷记录 |
| [xemu-plan.md](xemu-plan.md) | v1 | XEMU 研发计划：自研 x86-64 参考模拟器、里程碑、接口、风险与验收 |
| [xemu-roadmap.md](xemu-roadmap.md) | v2 | XEMU 开发计划 v2：对齐 NEMU 功能、Unicorn 指令验证标准、P1–P6 阶段与验收 |
| [xemu-linux-plan.md](xemu-linux-plan.md) | v3 | XEMU Linux 启动计划：L0–L4 里程碑（端口 IO/设备、boot protocol、x87、内存权限、工具链） |
| usage.md | 规划中 | 构建、仿真、调试指南 |

## 工程当前状态

| 里程碑 | 状态 | 说明 |
| --- | --- | --- |
| M0 工具链与骨架 | ✅ 已完成 | sbt + Chisel 3.5.6、Verilator 5.04、Unicorn 2.1.4；`EmitVerilog` 可生成 Verilog 并通过 Verilator lint |
| M1 解码器 | ✅ 已完成 | 693/693 条编码与 llvm-mc 长度/合法性一致 |
| M2 顺序整数核 | ✅ 已完成 | 37/37 汇编 difftest 通过；详见 [stage-a.md](stage-a.md) |
| M3 SSE2 + 系统 | ✅ 基本完成 | SSE2、分页（数据+取指）、IDT/IRETQ、MSR/SYSCALL、CPUID、INVLPG、STD、LAHF/SAHF/LOOP/INT n 完成；10/10 系统测试；LGDT/用户态/大页 待补 |
| M4 乱序前端/重命名 | ✅ v1 | 取指/解码/重命名（GPR+flags）/ROB/FreeList 已实现；OoO 6/6 差分通过 |
| M5 乱序后端 | 🚧 v3.8 | 双发射端口 + 2 宽提交、MOV/SSE/ALU 内存源与目的（RMW）、XCHG/XADD、直接/间接 CALL/RET/JMP、循环/条件跳转、MOV CR、分页、MUL/DIV、CDQ/CBW 完成；双宽译码/精确异常 待补 |
| M6 逐指令 DiffTest（emu） | 🚧 v1 | 香山式 `build/emu -i ... --diff --ref-so=...`；NEMU 风格 ABI + Unicorn 参考模型；37/37 定向 + 360/360 随机用例逐指令对拍（GPR/XMM/RFLAGS/内存）；详见 [verification.md](verification.md)、[emu_testplan.md](emu_testplan.md) |
| M7 定版 | ⬜ 未开始 | |

## 环境说明

本机环境有两个需要注意的点：

1. **sbt 的用户主目录不可写**：需要用 `-Duser.home` 指到工程内目录（脚本见根目录 `Makefile`，规划中）：

   ```bash
   SBT_OPTS="-Duser.home=$PWD/.home \
     -Dsbt.boot.directory=$PWD/.home/.sbt/boot \
     -Dsbt.global.base=$PWD/.home/.sbt \
     -Dsbt.ivy.home=$PWD/.home/.ivy2 \
     -Dsbt.coursier.home=$PWD/.home/.coursier" \
   sbt compile
   ```

2. **Chisel 使用 3.5.6 + Scala 2.13.10**：该组合使用旧版 Scala FIRRTL emitter，不依赖 `firtool` 二进制；如需升级 Chisel 6，需要额外解决 CIRCT 工具链。

## 参考

- [香山昆明湖 V3 设计文档](https://docs.xiangshan.cc/projects/design/zh-cn/kunminghu-v3/)
- [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
