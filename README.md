# OpenX86

[中文](#中文) | [English](#english)

**用 Chisel 实现的 x86-64 处理器（含 SSE2 子集），参考香山昆明湖微架构，配套香山/NEMU 风格逐指令 DiffTest 与 `emu`。**

**A Chisel implementation of an x86-64 processor (SSE2 subset), inspired by XiangShan Kunminghu, with a XiangShan/NEMU-style per-instruction DiffTest and `emu`.**

---

## 中文

### 简介

OpenX86 包含两个可仿真的 x86-64 处理器版本：

- **Stage-A 顺序核**：变长前端、五级流水、整数 + SSE2、4 级分页、异常/IDT/IRETQ、SYSCALL/CPUID/INVLPG
- **Stage-B 乱序核**：重命名、ROB、发射队列、PRF、StoreBuffer、双发射端口 + 2 宽提交、数据/取指分页

验证体系包括：Unicorn 结束态对拍、Stage-B vs Stage-A 对拍、系统态测试，
以及 **NEMU 风格逐指令 DiffTest**（`build/emu` + `libx86ref.so`）。

### 快速开始

前置：JDK、sbt、Verilator 5、clang、Python 3；Python 侧需要 `unicorn==2.1.4`。

```bash
make decode-test          # 解码向量 693/693
make regress              # Stage-A vs Unicorn 37/37
make sys-test             # 分页/异常/IDT 等系统测试 10/10
make ooo-test             # Stage-B vs Stage-A 29/29

make emu                  # 构建 build/emu + build/ref/libx86ref.so
make emu-test             # 37 个用户态用例逐指令对拍
make emu-fuzz             # 随机程序 fuzz（默认 200 个种子）

./build/emu -i build/tests/t1_basic.bin        # 香山风格入口
./build/emu -i build/tests/t1_basic.bin --no-diff
```

> `build/emu` 默认使用自研 **XEMU**（`xemu/`）。Unicorn 参考模型仍可选用：
> `make ref-unicorn` 后 `./build/emu --ref-so=build/ref/libx86ref.so ...`。
> Unicorn 的构建默认使用仓库内 `.home/pylibs/unicorn`（本机 vendored）。
> 若使用系统 Python 包，可设置：
> `make ref-so UNICORN_DIR=$(python3 -c 'import unicorn,os;print(os.path.dirname(unicorn.__file__))')`

### 验证结果（v0.2.0）

| 套件 | 结果 |
| --- | --- |
| 解码向量 | 693 / 693 |
| Stage-A vs Unicorn（结束态） | 37 / 37 |
| 系统测试 | 10 / 10 |
| Stage-B vs Stage-A（结束态） | 29 / 29 |
| emu 逐指令对拍（默认参考模型 XEMU） | 37 / 37 |
| XEMU 系统模式 co-sim（含分页/异常） | 10 / 10 |
| 随机 fuzz 逐指令对拍（XEMU） | **1000 / 1000** |
| Scala 单测 | 21 / 21 |

### 目录

| 路径 | 内容 |
| --- | --- |
| `src/main/scala/x86/` | Chisel RTL：ISA、前端、顺序核、乱序核、MMU、执行单元 |
| `sim/` | Verilator testbench 与 DUT 侧 difftest glue |
| `ref/` | NEMU 风格参考模型（`.so`，Unicorn 语义）与 ABI |
| `tests/asm` | Stage-A 定向汇编测试 |
| `tests/system` | 分页/异常/IDT/SYSCALL 等系统测试 |
| `tests/ooo` | Stage-B vs Stage-A 对拍用例 |
| `tools/` | difftest、emu 回归、fuzz、汇编/仿真脚本 |
| `docs/` | 中文设计与验证文档 |

### 文档

- [docs/README.md](docs/README.md)：文档索引与里程碑
- [docs/microarchitecture.md](docs/microarchitecture.md)：微架构手册
- [docs/stage-a.md](docs/stage-a.md)：顺序核结构与指令覆盖
- [docs/verification.md](docs/verification.md)：测试矩阵、性能、限制
- [docs/emu_testplan.md](docs/emu_testplan.md)：emu 逐指令测试计划
- [xemu/README.md](xemu/README.md)：XEMU（自研参考模拟器）
- [xemu/docs/plan.md](xemu/docs/plan.md)：XEMU 研发计划
- [xemu/docs/roadmap.md](xemu/docs/roadmap.md)：XEMU 路线图（P1–P6）
- [xemu/docs/linux-plan.md](xemu/docs/linux-plan.md)：XEMU Linux 启动计划（L0–L4）
- [xemu/docs/linux-testplan.md](xemu/docs/linux-testplan.md)：XEMU Linux 测试用例与门禁

### 已知限制

- 前端仍为 1 宽译码/派发；ROB/IQ/PRF 规模小于昆明湖 V3
- Stage-B 尚无精确异常/IDT
- 字符串指令、内存间接 CALL 未实现
- `emu --diff` 基于 Unicorn，只覆盖 paging 关闭的用户态用例
- 尚未添加 LICENSE（待定）

---

## English

### Overview

OpenX86 contains two simulatable x86-64 processor variants:

- **Stage-A (in-order)**: variable-length frontend, 5-stage pipeline, integer + SSE2,
  4-level paging, exceptions/IDT/IRETQ, SYSCALL/CPUID/INVLPG
- **Stage-B (out-of-order)**: rename, ROB, issue queue, PRF, store buffer,
  a second issue port + 2-wide commit, data/fetch paging

Verification includes end-state comparison against Unicorn, Stage-B vs Stage-A,
system tests, and a **NEMU-style per-instruction DiffTest**
(`build/emu` + `libx86ref.so`).

### Quick start

Prerequisites: JDK, sbt, Verilator 5, clang, Python 3 with `unicorn==2.1.4`.

```bash
make decode-test          # decode vectors 693/693
make regress              # Stage-A vs Unicorn 37/37
make sys-test             # paging/exception/IDT system tests 10/10
make ooo-test             # Stage-B vs Stage-A 29/29

make emu                  # build build/emu (default reference: build/ref/libxemu.so)
make emu-test             # per-instruction check of 37 user-mode tests
make emu-fuzz             # random-program fuzzing (200 seeds by default)

./build/emu -i build/tests/t1_basic.bin
./build/emu -i build/tests/t1_basic.bin --no-diff
```

> `build/emu` uses the self-written **XEMU** (`xemu/`) by default. The Unicorn
> reference model remains available: `make ref-unicorn`, then
> `./build/emu --ref-so=build/ref/libx86ref.so ...`.
> The Unicorn build uses the vendored `.home/pylibs/unicorn` by default.
> To use a system-wide Python package:
> `make ref-so UNICORN_DIR=$(python3 -c 'import unicorn,os;print(os.path.dirname(unicorn.__file__))')`

### Results (v0.2.0)

| Suite | Result |
| --- | --- |
| Decode vectors | 693 / 693 |
| Stage-A vs Unicorn (end state) | 37 / 37 |
| System tests | 10 / 10 |
| Stage-B vs Stage-A (end state) | 29 / 29 |
| emu per-instruction (default reference: XEMU) | 37 / 37 |
| XEMU system-mode co-sim (paging/exceptions) | 10 / 10 |
| Random fuzz (XEMU, per-instruction) | **1000 / 1000** |
| Scala unit tests | 21 / 21 |

### Layout

| Path | Contents |
| --- | --- |
| `src/main/scala/x86/` | Chisel RTL: ISA, frontend, in-order/OoO cores, MMU, execution units |
| `sim/` | Verilator testbenches and DUT-side difftest glue |
| `ref/` | NEMU-style reference model (`.so`, Unicorn semantics) and ABI |
| `tests/asm` | Directed Stage-A assembly tests |
| `tests/system` | Paging/exception/IDT/SYSCALL system tests |
| `tests/ooo` | Stage-B vs Stage-A comparison tests |
| `tools/` | difftest, emu regression, fuzz, assembler/simulation scripts |
| `docs/` | Design and verification documents (Chinese) |

### Documentation

- [docs/README.md](docs/README.md) — document index and milestones
- [docs/microarchitecture.md](docs/microarchitecture.md) — microarchitecture manual
- [docs/stage-a.md](docs/stage-a.md) — in-order core structure and ISA coverage
- [docs/verification.md](docs/verification.md) — test matrix, performance, limitations
- [docs/emu_testplan.md](docs/emu_testplan.md) — emu per-instruction test plan
- [xemu/README.md](xemu/README.md) — XEMU (self-written reference model)
- [xemu/docs/plan.md](xemu/docs/plan.md) — XEMU development plan
- [xemu/docs/roadmap.md](xemu/docs/roadmap.md) — XEMU roadmap (P1-P6)
- [xemu/docs/linux-plan.md](xemu/docs/linux-plan.md) — XEMU Linux boot plan (L0-L4)
- [xemu/docs/linux-testplan.md](xemu/docs/linux-testplan.md) — XEMU Linux test cases and gates

### Known limitations

- 1-wide decode/dispatch frontend; ROB/IQ/PRF smaller than Kunminghu V3
- No precise exceptions/IDT in Stage-B yet
- String instructions and memory-indirect CALL are not implemented
- `emu --diff` uses Unicorn and only covers user-mode tests with paging disabled
- No LICENSE file yet (TBD)
