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

## M2 状态（整数执行已完成，SSE 待 M4）

- 已实现：MOV/MOVZX/MOVSX/MOVSXD/LEA、ALU、移位/旋转、INC/DEC/NEG/NOT、
  MUL/IMUL/DIV/IDIV、CDQ/CQO/CBW、XCHG/XADD、CMP/TEST/SETcc/CMOVcc、
  JMP/JCC/LOOP/LOOPNE/LOOPE/JRCXZ、CALL/RET、PUSH/POP/PUSHF/POPF、LAHF/SAHF、
  STI/CLI/STD/CLD/CLC/STC/CMC、HLT
- 部分写、flags（含未定义位保持旧值）、内存寻址、RIP 相对、栈
- `make xemu-regress`：**27/37** 通过；剩余 10 个均为 SSE/COMIS 用例（M4）

## 下一步

实现完整前缀/REX/ModRM/SIB/disp/imm 解码，并用 693 条解码向量对齐 RTL。
