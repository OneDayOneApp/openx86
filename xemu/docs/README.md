# XEMU 文档

XEMU 是自研 x86-64 参考模拟器，导出 NEMU 风格 `difftest_*` ABI，用于替换 `emu`
中的 Unicorn 参考层，并作为独立运行器（`build/xemu`）使用。

| 文档 | 状态 | 说明 |
| --- | --- | --- |
| [plan.md](plan.md) | v1 | XEMU 研发计划：自研参考模型的范围、架构、M0–M6 里程碑、接口、风险与验收 |
| [roadmap.md](roadmap.md) | v2 | 路线图：对齐 NEMU 功能，Unicorn 作为指令验证标准，P1–P6 阶段 |
| [linux-plan.md](linux-plan.md) | v3 | Linux 启动计划：L0–L4（端口 IO/设备、boot protocol、x87、内存权限、工具链） |
| [linux-testplan.md](linux-testplan.md) | v1 | Linux 测试计划：156 个用例（TC 编号）、回归门禁、失败归档规范 |
| [architecture.md](architecture.md) | v1 | **软件架构图**：五层结构、模块清单、数据流、存储层、三种使用形态、验证层级、已知偏离与缺口 |
| [linux-status.md](linux-status.md) | v1 | **实测状态**：已能启动 Linux 到用户态；12 个致命缺陷的定位与修复、诊断工具、多 Agent 分工 |

相关文档：

- [../README.md](../README.md)：XEMU 概览、构建与当前测试结果
- [../../docs/emu_testplan.md](../../docs/emu_testplan.md)：emu 逐指令对拍测试计划
- [../../docs/verification.md](../../docs/verification.md)：整机验证手册

## 当前状态速查

| 项 | 结果 |
| --- | --- |
| **Linux 启动（T4）** | **✅ 用户态 `/init` 跑通** |
| ISA 对拍（Unicorn） | 18600 / 18600 |
| 用户态逐指令 | 37 / 37 |
| 系统逐指令锁步 | 10 / 10 |
| 系统 co-sim | 10 / 10 |
| XEMU 专属系统测试（CPL3/timer/#PF/SGDT/位操作） | 9 / 9 |
| 独立运行器 | `make xemu-bin` → `build/xemu` |
| Bochs 独立仲裁 | 39 / 39（`make bochs-test`） |

## 测试命令

```bash
make isa-diff           # Unicorn 指令级对拍
make xemu-test          # 用户态逐指令
make xemu-regress       # XEMU 定向回归
make xemu-sys-test      # CPL3 / TSS / timer / #PF / SGDT / 位操作
make xemu-linux-test    # **T4：真实 Linux → 用户态 /init**
make xemu-isa-scan      # 静态扫描内核镜像里的未实现指令
make sys-lockstep-test  # 系统逐指令锁步（RTL + XEMU）
make sys-diff-test      # 系统结束态 co-sim
make xemu-all-test      # 以上组合
make bochs-build        # 构建 Bochs 2.8 黄金基准
make bochs-test         # Bochs 独立仲裁对拍
```

## 黄金基准

三个参考模型定位互补，出现分歧时的仲裁顺序是
**Intel SDM > Bochs > QEMU > Unicorn**：

| 基准 | 覆盖 | 用途 |
| --- | --- | --- |
| [Unicorn](https://github.com/unicorn-engine/unicorn) | 用户态指令语义 | `make isa-diff`，14300 向量，日常/CI |
| [Bochs](https://bochs.sourceforge.io/) | 完整系统模式 + 调试器 | 独立实现，语义仲裁；`make bochs-test` |
| QEMU | 系统模式 / Linux 引导 | 后续 L1–L4 对照（未接入） |

Bochs 与 QEMU/Unicorn（同为 TCG）不同源，是独立解释器实现，
因此能捕捉"引擎级"偏差。它较慢（约 30 MIPS），故只做抽样仲裁，
不进每次提交的门禁。

### Bochs 接入方式

`tools/bochs/boot.S` 是一段 512 字节引导桩：建好 4 级恒等页表（0–16MiB，
外加 `0xF0000000` 映射到 scratch RAM 以承接 halt MMIO 写入）、置
`CR4.PAE|OSFXSR|OSXMMEXCPT`、打开 `LME`+`PG`，进入 64 位长模式后停住。
`tools/bochs/run.py` 随后用调试器 `loadmem` 装载载荷、清零寄存器、
通过一段 24 字节 stub 设置 `RFLAGS`（Bochs 的 `set eflags` 文档存在但未实现），
再单步执行并抓取寄存器快照。
