# XEMU

自研 x86-64 参考模拟器，导出 NEMU 风格 `difftest_*` ABI，用于替换 `emu` 中的 Unicorn 参考模型。
总体计划见 [../docs/xemu-plan.md](../docs/xemu-plan.md)。

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

## ISA 指令级对拍（Unicorn 标准）

```bash
make isa-diff
# isa diff: 13900/13900 passed, 0 failed
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
