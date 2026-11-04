# OpenX86

**用 Chisel 实现的 x86-64 处理器（含 SSE2 子集），参考香山昆明湖微架构，配套香山/NEMU 风格逐指令 DiffTest 与 `emu`。**

项目包含两个可仿真版本：

- **Stage-A 顺序核**：变长前端、五级流水、整数 + SSE2、分页、异常/IDT/IRETQ、SYSCALL/CPUID/INVLPG
- **Stage-B 乱序核**：重命名、ROB、发射队列、PRF、StoreBuffer、双发射端口 + 2 宽提交、数据/取指分页

验证体系包括：Unicorn 结束态对拍、Stage-B vs Stage-A 对拍、系统态测试、
以及 **NEMU 风格逐指令 DiffTest**（`build/emu` + `libx86ref.so`）。

## 快速开始

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

> 参考模型 `.so` 的构建默认使用仓库内 `.home/pylibs/unicorn`（本机 vendored）。
> 若使用系统 Python 包，可设置：
> `make ref-so UNICORN_DIR=$(python3 -c 'import unicorn,os;print(os.path.dirname(unicorn.__file__))')`

## 目录

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

## 验证结果（当前）

| 套件 | 结果 |
| --- | --- |
| 解码向量 | 693 / 693 |
| Stage-A vs Unicorn（结束态） | 37 / 37 |
| 系统测试 | 10 / 10 |
| Stage-B vs Stage-A（结束态） | 29 / 29 |
| emu 逐指令对拍（GPR/XMM/RFLAGS/内存） | 37 / 37 |
| 随机 fuzz 逐指令对拍 | 200/批，累计 360/360 |
| Scala 单测 | 21 / 21 |

## 文档

- [docs/README.md](docs/README.md)：文档索引与里程碑
- [docs/microarchitecture.md](docs/microarchitecture.md)：微架构手册（Stage-A / Stage-B / 与昆明湖 V3 对照）
- [docs/stage-a.md](docs/stage-a.md)：顺序核结构与指令覆盖
- [docs/verification.md](docs/verification.md)：测试矩阵、性能观察、已知限制
- [docs/emu_testplan.md](docs/emu_testplan.md)：emu 逐指令测试计划与缺陷记录
- [docs/PRD.md](docs/PRD.md)：需求文档

## 已知限制

- 前端仍为 1 宽译码/派发；ROB/IQ/PRF 规模小于昆明湖 V3（160/大容量/224-192-128）
- Stage-B 尚无精确异常/IDT（缺页与非法指令直接 fatal）
- 字符串指令、内存间接 CALL 未实现
- `emu --diff` 依赖 Unicorn，目前只覆盖 paging 关闭的用户态用例；系统态由 `make sys-test` 覆盖

## 说明

本项目是**参考**香山昆明湖微架构的独立实现，不包含香山 RTL。
Unicorn 作为运行时依赖以动态库方式加载，其许可证见上游项目。
