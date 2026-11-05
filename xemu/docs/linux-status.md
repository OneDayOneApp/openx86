# XEMU Linux 启动 — 实况状态与作战计划

本文是 [linux-plan.md](linux-plan.md) / [linux-testplan.md](linux-testplan.md) 的
**执行侧**跟踪页：记录当前卡点、诊断手段、多 Agent 分工与验收门禁。

更新原则：每次调试出结论就改这里，不写“应该/可能”，只写实测。

---

## 1. 当前基线（2026-11-05）

| 项 | 状态 |
| --- | --- |
| ISA 对拍（Unicorn） | 14300/14300 ✅ |
| `make xemu-dev-test`（xemu_sys.py） | 7/7 ✅（t40/t41/t50/t51/t52/t60/t61） |
| `emu-test` / `make emu-regress` | 37/37 ✅ |
| **`make xemu-linux-test`（T4）** | **PASS ✅ 真实 Linux 6.6.60 跑到用户态 `/init` 并完成** |
| **交互式 busybox shell** | **✅ 手动验证：banner + `/ #` 提示符 + `echo` 内建命令 + `poweroff`** |
| fork 子进程 | ❌ 子进程在 exec 前 `segfault at 1 ip 1`（用户态 `ret` 弹出 1） |

`make xemu-linux-test` 实测串口输出：

```
Linux version 6.6.60 ...                       (0.44M 指令)
Memory: 505760K/523896K available
printk: console [ttyS0] enabled
APIC: Keep in PIC mode(8259)
Unpacking initramfs...
Write protecting the kernel read-only data: 6144k
Run /init as init process                      (≈27.4M 指令)
XEMU: /init running as pid 1
XEMU: initramfs mounted, shutting down
reboot: System halted
```

约 **4.2 MIPS**，整轮 30M 指令 ≈ 7 s（Apple M 系列，单线程解释执行）。

---

## 2. 从 30 万到用户态：本次定位并修复的 12 个缺陷

每个都是“跑到某处突然崩”，用 `--trace-exc` / `--watch-mem` / `--stop-at` /
Bochs 逐指令对拍定位。按发现顺序：

| # | 缺陷 | 症状 | 影响面 |
| --- | --- | --- | --- |
| 1 | `decode.c` `groups[G_15-1]` 越界（数组 7 行，索引 8） | 写坏相邻 `.bss`（x87 状态） | 潜在随机错误 |
| 2 | 异常帧用**物理地址**压栈（`xemu_mem_write`） | 开页后内核高地址栈帧写丢，`iret` 到垃圾 | 所有开页异常 |
| 3 | 异常帧 CS/SS 硬编码 0/8 | Linux 看到 `__KERNEL32_CS`，走 32 位返回路径 | 早期 #PF 即崩 |
| 4 | **AH/CH/DH/BH 未实现**，一律别名到低字节 | `testb $0x10,%ah` 读错 → LA57 分支误判 | printk 宽度错乱 |
| 5 | `MOV r/m16, Sreg`（8C）方向写反 | `movl %cs,%eax` 是空操作 → IRET 帧 CS 变代码地址 | IRET 返回路径 |
| 6 | 4KB PTE 未屏蔽 **NX 位**（bit 63 折进物理地址） | NX 一开，所有 4KB 数据页不可读 | 直接映射、fixmap |
| 7 | **BT/BTS/BTR/BTC 内存操作数**不按位偏移前进地址 | `system_vectors` 位图被写错，IDT 0x30 未安装 | 中断注入 |
| 8 | `0F AE` 被当作 x87 转义组 | FXSAVE/FXRSTOR/CLFLUSH/MXCSR 全部解码成垃圾 | 上下文切换 |
| 9 | **#PF 错误码缺 U/S 位** | 用户态缺页被当内核缺页 → `page_fault_oops` | 用户态无法启动 |
| 10 | GDT 描述符 base 只取低 32 位 | TSS 在 CPU-entry area（`0xfffffe0000…`）→ base 变 `0x3000` | 一切 CPL3 陷阱 |
| 11 | TSS `rsp0` 被截成 32 位 | 首次用户态陷阱压栈到 `0x2fc8` | 同上 |
| 12 | 8259A **ICW1 清 IMR** | ICW 序列期间 IRQ0 被投递到未安装的 IDT 项 | 早期定时器中断 |

外加两个设备模型补齐：

| 项 | 说明 |
| --- | --- |
| 16550 **THRE 中断** | 之前 IIR 恒为 0x01，tty 层写完 1 字节就等中断，用户态 `write(2)` 只剩首字节；现在 THR 写完置 THRE 并按 IER 拉 IRQ4 |
| 8259A OCW2 级别字段 | 特指 EOI 的 level 是 bits 2:0，原先读成 bits 4:2 |

### 新增指令（内核路径实测必需）

`SGDT` `SIDT` `SMSW` `LMSW` `SLDT` `STR` `LLDT` `VERR` `VERW` `SWAPGS`
`RDTSC` `RDTSCP` `RDPMC` `CLTS` `PAUSE` `BSWAP` `XGETBV` `CLAC` `STAC`
`SHLD` `SHRD` `POPCNT` `MOVNTI` `PREFETCH` `WBINVD` `EMMS` `FWAIT`
`RDRAND` `RDSEED` `LAR` `XSETBV` `XSAVES` `XSAVEC` `XRSTORS` `LKGS`
`MONITOR` `MWAIT` `TILERELEASE` `RD/WRFSBASE` `RD/WRGSBASE` `UD1`

---

## 3. 诊断基建（本次新增，长期保留）

| 工具 | 用途 |
| --- | --- |
| `--trace-pc=FILE` | 只记 PC，和 Bochs 轨迹对拍 |
| `--trace-exc` | 每次异常投递打印 `vec/err/pc/cr2/cpl/cs/ss/rsi/rdi/rax` |
| `--stop-at-exc=N` | 第 N 次异常投递处停机 |
| `--stop-at=ADDR` | 到地址停机并 dump 寄存器 |
| `--watch-mem=LO:HI` / `--watch-byte=XX` / `--watch-after=N` | 物理写观察点，抓“谁写坏了这块内存” |
| `--dump-vmem` | 过 MMU 的十六进制 dump |
| `--serial-stop=STR` | 串口出现某串就停（门禁用） |
| `make xemu-isa-scan IMAGE=…` | 静态扫描内核镜像里 XEMU 不认识的指令（一次列全，不用逐次崩溃试） |
| `tools/bochs/kernel_boot.py` + `trace_compare.py` | 用 Bochs 作为独立仲裁，逐指令 PC 轨迹对拍 |

---

## 4. 多 Agent 分工（本次）

| Agent | 交付 |
| --- | --- |
| **A 内核镜像** | `tools/linux/build_kernel.sh`：Docker(arm64)+`gcc-x86-64-linux-gnu` 交叉编译 6.6.60 tinyconfig 内核（`.symtab` 完整）+ `mkinitramfs.py` 生成静态 `/init` |
| **B Bochs 仲裁** | `tools/bochs/boot_kernel.S` / `kernel_boot.py` / `trace_compare.py`：Bochs 引导同一 vmlinux 并产出逐指令轨迹；查出“重构 ELF 的 percpu 段丢 LMA”等问题 |
| 主线 | 全部 `xemu/` 修复、诊断工具、门禁、文档 |

Bochs 轨迹与 XEMU 在 22291 条指令内完全一致（之后只在 CPUID 扩展叶上分叉），
说明差异来自 CPUID 特征而不是指令语义。

---

## 5. 已知缺口（不影响当前门禁）

| 项 | 说明 |
| --- | --- |
| `SGDT`/`SIDT` 64 位伪描述符 | oops 打印里 IDT/GDT 是乱的；内核/用户态运行不受影响，下一步修并加测试 |
| `RETF` 不校验选择子 | 真实硬件对空描述符 `#GP`，XEMU 直接接受（Bochs 对拍发现） |
| 无 U/S 权限检查、无 A/D、无 CR0.WP | L3 范围；当前靠 `err` 的 U/S 位把用户缺页交给内核 |
| `em` 缺 `FS/GS` base 的 U/S | 同上 |
| 性能 | ~4 MIPS，可接受；需要交互 shell 时再优化 |

---

## 6. 门禁

```bash
make xemu-dev-test      # 7/7
make isa-diff           # 14300/14300
make emu-test           # 37/37
make sys-lockstep-test  # 10/10（difftest 侧用 RTL 兼容帧模式）
make xemu-linux-test    # T4：真实 Linux → 用户态 /init
```

TDD 顺序固定：**先写失败的用例 → 修 → 跑门禁 → 提交**。
