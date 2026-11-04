# XEMU 文档

XEMU 是自研 x86-64 参考模拟器，导出 NEMU 风格 `difftest_*` ABI，用于替换 `emu`
中的 Unicorn 参考层，并作为独立运行器（`build/xemu`）使用。

| 文档 | 状态 | 说明 |
| --- | --- | --- |
| [plan.md](plan.md) | v1 | XEMU 研发计划：自研参考模型的范围、架构、M0–M6 里程碑、接口、风险与验收 |
| [roadmap.md](roadmap.md) | v2 | 路线图：对齐 NEMU 功能，Unicorn 作为指令验证标准，P1–P6 阶段 |
| [linux-plan.md](linux-plan.md) | v3 | Linux 启动计划：L0–L4（端口 IO/设备、boot protocol、x87、内存权限、工具链） |
| [linux-testplan.md](linux-testplan.md) | v1 | Linux 测试计划：156 个用例（TC 编号）、回归门禁、失败归档规范 |

相关文档：

- [../README.md](../README.md)：XEMU 概览、构建与当前测试结果
- [../../docs/emu_testplan.md](../../docs/emu_testplan.md)：emu 逐指令对拍测试计划
- [../../docs/verification.md](../../docs/verification.md)：整机验证手册

## 当前状态速查

| 项 | 结果 |
| --- | --- |
| ISA 对拍（Unicorn） | 14300 / 14300 |
| 用户态逐指令 | 37 / 37 |
| 系统逐指令锁步 | 10 / 10 |
| 系统 co-sim | 10 / 10 |
| XEMU 专属系统测试（CPL3/timer） | 2 / 2 |
| 独立运行器 | `make xemu-bin` → `build/xemu` |

## 测试命令

```bash
make isa-diff           # Unicorn 指令级对拍
make xemu-test          # 用户态逐指令
make xemu-regress       # XEMU 定向回归
make xemu-sys-test      # CPL3 / TSS / timer
make sys-lockstep-test  # 系统逐指令锁步（RTL + XEMU）
make sys-diff-test      # 系统结束态 co-sim
make xemu-all-test      # 以上组合
```
