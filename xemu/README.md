# XEMU

自研 x86-64 参考模拟器，导出 NEMU 风格 `difftest_*` ABI，用于替换 `emu` 中的 Unicorn 参考模型。
文档：[计划](docs/plan.md)、[路线图](docs/roadmap.md)、[Linux 启动计划](docs/linux-plan.md)、
[Linux 测试计划](docs/linux-testplan.md)、**[Linux 实测状态](docs/linux-status.md)**。

## 现状：能启动真实 Linux 并进入交互式 shell（2026-11-05）

`make xemu-linux-test` 全绿：Linux 6.6.60 走 64 位引导协议，进入用户态
`/init` 并完成关机流程。

```
Run /init as init process
...
XEMU-SHELL-READY
/ # ls /bin
[         date      halt      mkdir     pwd      sort    uname
ash       df        head      mknod     reboot   sync    vi
...
/ # cat /proc/cpuinfo | head -3
processor       : 0
vendor_id       : X86-KMH-Core
cpu family      : 6
/ # poweroff -f
reboot: System halted
```

| 门禁 | 结果 |
| --- | --- |
| `make xemu-linux-test`（T4，真实内核→用户态） | ✅ 30M 指令 / 2.2 s / ~14 MIPS（持续 ~22 MIPS） |
| `make isa-diff`（Unicorn 逐指令） | 18600/18600 |
| `make xemu-decode-test`（长度+字段） | 693/693 + 693/693 |
| `make emu-test`（DUT vs XEMU 逐指令） | 37/37 |
| `make sys-lockstep-test`（系统逐指令） | 10/10 |
| `make xemu-dev-test`（XEMU 专属系统/设备测试） | 19/19 |
| `make xemu-shell-test`（T5：交互式 busybox shell，含 applet 管道） | ✅ PASS |
| `make xemu-soak-test`（L4：10 分钟无 hang） | ✅ 72 条命令全部响应 |

```bash
make xemu-linux-runner          # build/xemu-linux（512 MiB RAM）
tools/linux/build_kernel.sh     # 交叉编译最小内核（Docker）
tools/linux/mkinitramfs.py      # 生成 initramfs
make xemu-linux-test            # T4 门禁（启动到 /init）
make xemu-shell-test            # T5 门禁（交互式 shell：echo/ls/cat/管道/poweroff）
make xemu-isa-scan IMAGE=build/linux/vmlinux   # 静态扫描未实现指令
```

## M0 状态（已完成）

- `xemu/` 骨架：state/mem/decode/exec/difftest 五个模块
- `build/ref/libxemu.so`（无 Unicorn 依赖）
- 最小指令子集：`MOV r64, imm64`（REX.W B8+rd）、`HLT`
- 已通过：

```bash
make xemu-so
python3 tools/difftest.py tests/emu/xemu_m0.S
./build/emu -i build/tests/xemu_m0.bin --ref-so=build/ref/libxemu.so
```

输出 `1 instructions cross-checked`，说明 DUT 与 XEMU 逐指令一致。

## M1 状态（已完成）

- 表驱动解码：前缀/REX/0F 映射/组表/ModRM/SIB/disp/imm/RIP 相对
- 字段级对齐 RTL：`len/op/simd/opsz/memsz/a/b/imm/base/index/disp/illegal`
- 已通过：

```bash
make xemu-decode-test
# xemu decode: 693/693 passed, 0 failed
# xemu decode fields: 693/693 passed, 0 failed
```

`xemu/decode_test.c` 对齐 693 条向量的长度/合法性；
`xemu/decode_field_test.c` 用 RTL `DUMP=1` 的输出做字段级对拍。

## 目录

| 文件 | 内容 |
| --- | --- |
| `xemu.h` | 状态、指令、内存接口 |
| `state.c` | 复位与全局状态 |
| `mem.c` | 16MB 物理内存 + MMIO 停机 |
| `decode.c` | M0 最小解码（M1 替换为表驱动） |
| `exec.c` | M0 最小执行 |
| `difftest.c` | NEMU 风格 ABI 导出 |

## M2 / M4 状态（整数 + SSE2 子集已完成）

- 已实现：MOV/MOVZX/MOVSX/MOVSXD/LEA、ALU、移位/旋转、INC/DEC/NEG/NOT、
  MUL/IMUL/DIV/IDIV、CDQ/CQO/CBW、XCHG/XADD、CMP/TEST/SETcc/CMOVcc、
  JMP/JCC/LOOP/LOOPNE/LOOPE/JRCXZ、CALL/RET、PUSH/POP/PUSHF/POPF、LAHF/SAHF、
  STI/CLI/STD/CLD/CLC/STC/CMC、HLT
- 部分写、flags（含未定义位保持旧值）、内存寻址、RIP 相对、栈
- `make xemu-regress`：**37/37** 通过（整数 + SSE2 子集）
- 随机 fuzz（XEMU 参考）：`XEMU_REF=build/ref/libxemu.so python3 tools/emu_fuzz.py 200 1` → 200/200

## M4 SSE2 子集

- 传送：MOVD/MOVQ/MOVDQ/MOVPS（含 MOVSS/MOVSD 高位语义）
- 整数 SIMD：PADDD/PSUBD/PAND/POR/PXOR
- 浮点：ADD/SUB/MUL/DIV/SQRT/MIN/MAX（SS/SD/PS/PD）、COMIS/UCOMIS
- 转换：CVTSI2SS/SD、CVTTSS2SI/CVTTSD2SI/CVTSD2SI、CVTSS2SD/CVTSD2SD、
  CVTDQ2PS/CVTPS2DQ/CVTDQ2PD/CVTPD2DQ/CVTTPD2DQ
- `t13`–`t21` 全部通过

## M3 状态（已完成，系统模式）

- 4 级页表 walk、CR0/2/3/4、LIDT、IDT 门、异常投递、IRETQ、SYSCALL/SYSRET、
  CPUID、MSR、INVLPG、#PS/#UD/#DE/#GP
- 物理寻址语义与 RTL sys 微码一致（IDT/陷阱栈绕过 MMU）
- `make sys-diff-test`：DUT + XEMU 同一镜像 co-sim，最终 GPR/RIP/RFLAGS 对比 **10/10**
- 逐指令 lockstep：**10/10**（`make sys-lockstep-test`）
  - 修复：UD2 在 EX 阶段触发 trap；sys 微码期间冻结流水线，避免 UD2 之后的指令投机退休

```bash
make sys-diff-test
# 10 x PASS
```

## L0 设备模型（端口 IO / UART / PIC / PIT）

- `IN`/`OUT`/`INS`/`OUTS`（8/16/32 位，含 `REP`），端口分发框架
- 16550 UART（`0x3F8`）：THR/RBR/IER/IIR/LCR/MCR/LSR/SCR/DLL/DLM
  - 写 THR 直通 stdout（`\n` → `\r\n`），stdin 非阻塞轮询进 RBR 并触发 IRQ4
- 8259A PIC（`0x20/0x21`、`0xA0/0xA1`）：ICW1-4、OCW1/OCW2/OCW3、IRR/ISR、级联
- 8254 PIT（`0x40-0x43`）：模式 2/3、锁存读、通道 0 → IRQ0
- 未映射端口读全 1、写丢弃
- 物理内存可配置：`-DXEMU_MEM_SIZE_MB=256`（默认 16）

```bash
make xemu-dev-test
# xemu sys: 5 passed, 0 failed
```

## P3 系统模式（CPL3 / TSS / 外部 IRQ / timer）

- `LGDT` / `LIDT` / `LTR`，GDT 描述符解析
- CPL0/CPL3 切换：特权级变化时通过 TSS `RSP0` 换栈，同层陷阱不换栈
- IDT 门 DPL 检查、中断门清 IF、陷阱门保留 IF
- 外部中断注入：`xemu_request_irq()`；每指令 `xemu_timer_tick()`
- 定时器 MMIO（`0xE0000000`）：
  - `+8` 周期，`+12` 使能，`+16` 当前计数，`+20` 清除 pending
  - 到期置 `irq_pending=32`，IF=1 时在下一条指令边界注入
- `SYSCALL`/`SYSRET` 按 STAR 切 CS/SS 与 CPL

```bash
make xemu-sys-test
# PASS t40_cpl3  (用户态 int 0x80 进入内核并 iretq 返回)
# PASS t41_timer (定时器中断重复注入)
```

## ISA 指令级对拍（Unicorn 标准）

```bash
make isa-diff
# isa diff: 14300/14300 passed, 0 failed
```

`tools/isa_diff.py` 对每条指令（含字符串/REP、BSF/BSR、BT 系列、CMPXCHG/8B/16B、SSE2 整数、间接 CALL/JMP）：
- 相同 GPR/XMM/RFLAGS/内存初始状态
- Unicorn 与 XEMU 各执行一条
- 比较 RIP/GPR/XMM/flags（未定义位掩码）/内存
- 标量 SS/SD 按 32/64 位 NaN 语义比较

## NEMU 风格独立运行（xemu CLI）

```bash
make xemu-bin
./build/xemu -i build/tests/t1_basic.bin -b        # batch
./build/xemu -i build/tests/t1_basic.bin           # interactive monitor
./build/xemu -i image.bin --trace --max-steps=1000 # itrace + 限制
```

Monitor 命令：`si [N]`、`info r`、`x N ADDR`、`q`。
批处理结束时输出 `halted after N instructions, rip=..., exit=...`；
执行失败时打印 NEMU 风格最后 16 条指令 ring buffer。

## M5 状态（已完成）

- `build/emu` 默认参考模型切换为 `build/ref/libxemu.so`（Unicorn 用 `make ref-unicorn` 可选）
- `make emu-test`：37/37
- 随机 fuzz：`python3 tools/emu_fuzz.py 1000 1` → **1000/1000**
- `make sys-diff-test`：10/10
- 发布 v0.2.0

## 下一步

后续：逐指令 trap 对拍、更多 SSE2/字符串指令、性能优化。
