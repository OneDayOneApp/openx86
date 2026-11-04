# 全球 x86 芯片公司地图

> 调研日期：2026-11-03  
> 范围：设计或销售 x86 / x86-64 通用处理器的公司，以及已退出/被收购的历史厂商  
> 说明：市场份额为 Mercury Research 2026 Q2 数据（经 TechPowerUp 转引）；“x86 兼容”不等于“获得 Intel/AMD 授权”，具体授权关系以公司披露为准。

## 0. 一句话结论

**当前仍在销售 x86 CPU/SoC 的公司，全球大约 8 家：**

- **两大巨头**：Intel、AMD（合计接近 100% 出货）
- **x86 授权/兼容厂商**：VIA（台湾）、兆芯 Zhaoxin（中国）、海光 Hygon（中国）
- **嵌入式/工业 x86 SoC**：DM&P（台湾，Vortex86）、RDC（台湾，EmKore）
- **Intel Xeon 衍生**：澜起科技 Montage（中国上海，津逮 Jintide）

如果把口径收紧为“拥有/继承 x86 微架构设计能力”，澜起不算（它的 CPU die 来自 Intel Xeon）；如果按“销售 x86 CPU 产品”口径，则应列入。

除此之外，SiS 只剩 legacy x86 技术，Centaur 团队被 Intel 收编，Cyrix/NexGen/Rise/Transmeta/IBM/TI/Harris/Siemens/ST/NEC 等均已退出或被收购。

---

## 1. 当前仍在做/卖 x86 的公司

### 1.1 总表

| 公司 | 国家/地区 | 定位 | 代表产品 | 备注 |
| --- | --- | --- | --- | --- |
| **Intel** | 美国 | x86 架构主导者、IDM | Core / Xeon / Atom | 2026 Q2 x86 份额 69.3%（PC+服务器） |
| **AMD** | 美国 | x86-64（AMD64）定义者、fabless | Ryzen / EPYC / Ryzen Embedded | 2026 Q2 份额 30.7%，历史新高 |
| **VIA** | 台湾 | x86 授权方、legacy CPU | Eden / C7 / Nano / Cyrix III | 2003 年与 Intel 达成专利交叉许可；x86 主线已转向兆芯 |
| **兆芯 Zhaoxin** | 中国上海 | x86 兼容通用 CPU | 开先 KX-7000、开胜 KH-50000 | VIA 与上海市政府合资起家，现称自主 ZX86 |
| **海光 Hygon** | 中国天津 | x86 服务器 CPU | Dhyana、C86 系列 | 2016 年获 AMD Zen 授权（中国境内销售）；2019 年被列入美国实体清单 |
| **DM&P Electronics** | 台湾 | 嵌入式 x86 SoC | Vortex86DX/DX2/DX3/EX/EX2 | 32 位 x86 兼容，工业/自动化，长生命周期 |
| **RDC Semiconductor** | 台湾 | 嵌入式 x86 SoC | EmKore R3600、R819300（64 位，2026） | 32/64 位 x86，Win7/DOS/Linux，工业/金融终端 |
| **澜起科技 Montage** | 中国上海 | x86 服务器 CPU（Intel Xeon 衍生） | 津逮 Jintide | 基于 Intel Xeon 内核，增加 PrC/DSC 安全功能；**不是自研 x86 核** |

> 如果把“只卖 legacy 产品、不再迭代”也算上，VIA 仍在列；如果只算“有新产品迭代”，则是 Intel、AMD、兆芯、海光、DM&P、RDC、澜起（津逮）七家。其中澜起是 Intel Xeon 衍生，不是自研 x86 核。

### 1.2 两大巨头

**Intel**

- x86 指令集的原创者；x86-64 通过与 AMD 的交叉授权实现。
- 产品覆盖 PC（Core Ultra）、服务器（Xeon 6/7）、嵌入式（Atom）。
- IDM 模式：自己设计、自己制造（Intel Foundry 也对外代工）。
- 2026 Q2 x86 份额 69.3%（PC+服务器），含 embedded/IoT 为 65.9%。

**AMD**

- x86-64（AMD64）的创造者；Intel 的 x86-64 实现来自双方交叉授权。
- 产品覆盖 PC（Ryzen）、服务器（EPYC）、嵌入式（Ryzen Embedded/EPYC Embedded）。
- Fabless：主要依赖 TSMC 代工。
- 2026 Q2 份额 30.7%，首次突破 30%；服务器 34.5%，桌面 34.9%，笔记本 28.9%。

### 1.3 x86 授权/兼容厂商

**VIA（台湾）**

- 通过收购 Cyrix、IDT/Centaur 获得 x86 团队与技术；产品包括 Cyrix III、C3、C7、Nano、Eden。
- 2003 年与 Intel 和解并签署十年专利交叉许可：Intel 授权 VIA 销售 **x86 指令集兼容**的处理器，但不得 pin/bus 兼容 Intel 处理器。
- 目前 VIA 官网仍列有 C3/C7/Nano/Eden 等产品，但基本属于 legacy/嵌入式长尾；新的 x86 主线由兆芯承接。
- Centaur 团队 2021 年被 Intel 以约 1.25 亿美元收编，Centaur 不再作为独立 x86 供应商。

**兆芯 Zhaoxin（中国上海）**

- 2013 年由 VIA 与上海市政府合资成立，承接 VIA 的 x86 授权与团队。
- 产品：
  - **开先 KX-7000**：8C/8T，3.7 GHz，32MB cache，双通道 DDR5，PCIe 4.0，USB4，集成显卡，AVX2，VT-x/VT-d。
  - **开胜 KH-50000**：最高 96C/96T，384MB L3，12 通道 DDR5，128×PCIe 5.0，Chiplet，多路互连；2026 MWC 展出。
- 官方称“自主 ZX86 指令集”，兼容 x86 32/64 位与 SSE4.2/AVX/AVX2；生态上仍按 x86 兼容产品销售。
- 主要市场：中国信创、政务、金融、通信、教育、能源等。

**海光 Hygon（中国天津）**

- 2016 年与 AMD 成立合资公司，AMD 授权 Zen 架构（中国境内销售），作价 2.93 亿美元；产品品牌为 Dhyana。
- 后续演进到 C86 系列；据 2026-06 报道/路线图，**C86-5G** 为 128C/512T（SMT4）、AVX-512、16 通道 DDR5-5600、PCIe 5.0、CXL 2.0。
- 2019 年被美国列入 Entity List，先进制程/IP 获取受限；主要市场是中国服务器/信创。
- 与兆芯并列为中国大陆仅有的两家 x86 通用 CPU 设计公司。

### 1.4 嵌入式/工业 x86 SoC

**DM&P（台湾）**

- 产品线：Vortex86DX、DX2、DX3、EX、EX2。
- **Vortex86DX3**：32 位 x86 双核，1 GHz，6 级流水，32KB L1 I/D + 512KB L2，DDR3，SATA/PCIe/ISA，720-pin BGA；工业级 -40~85°C。
- **Vortex86EX2**：异构双核（Master 600 MHz + Slave 400 MHz），两个独立 BIOS/OS，面向实时工业控制。
- 定位：不是高性能 x86，而是“长生命周期、低功耗、接口丰富、DOS/WinCE/Linux/RTOS 兼容”的工业 SoC。
- 具体 x86 授权关系未公开；产品以“x86 compatible”表述。

**RDC（台湾）**

- 产品线：EmKore 32-bit / 64-bit x86 平台。
- **EmKore 32-bit**：R3600 双核 1 GHz，40nm，DDR3，2D GPU，Win7/DOS/Linux/VxWorks。
- **EmKore 64-bit**：R819300 双核 x86，支持 Win7，2026-09 宣布“coming soon”。
- 定位：POS、ATM、HMI、数字标牌、瘦客户机、车载娱乐等工业/商业终端。
- 具体 x86 授权关系未公开；产品以“x86-capable/ x86 processor platform”表述。

### 1.5 特殊案例：澜起科技 / 津逮（Jintide）

- 澜起科技（Montage Technology，上海，688008.SH）的主业是**内存接口芯片**（DDR4/DDR5 RCD、CKD、MRCD/MDB）、**PCIe/CXL Retimer**、**时钟芯片**；津逮是其服务器 CPU 产品线。
- 官方定义：津逮是“具有安全预检测（PrC）或动态安全监控（DSC）功能的 x86 架构处理器”；英文页明确写 **“Designed based on Intel® Standard Xeon® processor core”**，功能、性能、可靠性与 Xeon 一致。
- 产品：第六代津逮（2026）最高 **86 核 P-core / 144 核 E-core**；第五代 C6548N 为 32C/64T、2.8/4.1 GHz、60MB cache、DDR5-5200、PCIe 5.0、LGA4677、AVX-512/AMX。
- 定位：不是自研 x86 微架构，而是“**Intel Xeon + 澜起安全/监控/IOH 技术**”的国产化服务器 CPU 品牌/平台；CPU die 来自 Intel。
- 归类：
  - 按“销售 x86 CPU 产品”口径：**算**，应列入（第 8 家）。
  - 按“设计 x86 核/拥有微架构”口径：**不算**。
  - 对“自主可控”讨论：津逮是“国产品牌/安全增强”，但核心仍依赖 Intel Xeon；与兆芯/海光“授权架构 + 后续演进”不同。
- 2019 年 Hot Chips 上公开，2026 年已迭代到第六代。

---

## 2. 历史 x86 厂商（已退出/被收购）

| 公司 | 国家/地区 | 代表产品 | 结局 |
| --- | --- | --- | --- |
| **Cyrix** | 美国 | 6x86、MediaGX | 1997 年被 National Semiconductor 收购；1999 年 x86 CPU 业务被 VIA 收购；MediaGX 谱系 → Geode |
| **NexGen** | 美国 | Nx586 | 1996 年被 AMD 收购；技术演化为 AMD K6 |
| **Rise Technology** | 美国 | mP6 | 1999 年被 SiS 收购，mP6 技术进入 SiS |
| **IDT / Centaur** | 美国 | WinChip | 1999 年 Centaur 被 VIA 收购；后续 C3/C7/Nano；2021 年团队被 Intel 收编 |
| **Transmeta** | 美国 | Crusoe、Efficeon | VLIW + 代码翻译的 x86 兼容路线；2009 年停止运营，专利出售 |
| **National Semiconductor** | 美国 | Geode | 2003 年 Geode 业务卖给 AMD |
| **IBM** | 美国 | 386SLC、486SLC、Blue Lightning | 依据与 Intel 的协议做 x86 兼容芯片；后退出 x86 CPU |
| **Texas Instruments** | 美国 | TI486SXL/SXL2 | 486 级嵌入式 x86；与 Cyrix 有诉讼；后退出 |
| **Harris Semiconductor** | 美国 | 80C286 | CMOS 80286；后并入 Intersil；退出 x86 |
| **Siemens / Infineon** | 德国 | SAB 80286 | 80286 兼容；后退出 x86 |
| **STMicroelectronics** | 欧洲 | STPC | 嵌入式 x86 SoC；已停产 |
| **NEC** | 日本 | V20 / V30 | 8086/8088 兼容；曾与 Intel 有微码版权诉讼；后退出 x86 |
| **SiS** | 台湾 | SiS550 | 1999 年收购 Rise 获得 mP6；2001 年推出信息家电 x86 SoC；现主营混合信号/触控/ASIC，不再做 x86 CPU |
| **VIA** | 台湾 | Cyrix III/C3/C7/Nano/Eden | 仍在授权/legacy 销售，但新 x86 主线转向兆芯 |
| **AMD Geode** | 美国 | Geode LX/CS | AMD 内部 legacy 嵌入式产品线，现主推 Ryzen Embedded |
| **Intel Quark** | 美国 | Quark、Edison、Galileo | 2013–2019 的 x86 SoC/开发板；已停产 |

> 这张表说明：x86 历史上出现过大量玩家，但 2000 年代后快速收敛到 Intel/AMD 双寡头；其余厂商要么被收购，要么退到嵌入式长尾。

---

## 3. 市场格局（2026 Q2）

Mercury Research 数据（仅 PC + 服务器出货，不含嵌入式/IoT 小厂）：

| 细分 | Intel | AMD |
| --- | ---: | ---: |
| PC + 服务器 | 69.3% | 30.7% |
| 含 semi-custom / embedded / IoT | 65.9% | 34.1% |
| 桌面 | 65.1% | 34.9% |
| 服务器 | 65.5% | 34.5% |
| 笔记本 | 71.1% | 28.9% |
| 整体 PC（AMD） | — | 30.3% |

要点：

- Intel 仍是第一，但份额已跌到 1995 年以来最低；AMD 首次突破 30%。
- 兆芯、海光主要在中国信创/服务器市场，全球出货份额很小；DM&P、RDC 属于工业长尾，量级更小。
- x86 的“新进入者”几乎为零：没有像 RISC-V 那样的创业潮。

---

## 4. 按国家/地区看

| 国家/地区 | 当前 x86 设计公司 | 历史/备注 |
| --- | --- | --- |
| 美国 | **Intel、AMD** | Cyrix、NexGen、Rise、IDT/Centaur、Transmeta、NS、IBM、TI、Harris 等已退出 |
| 台湾 | **VIA、DM&P、RDC** | SiS 只剩 legacy x86 技术 |
| 中国大陆 | **兆芯、海光、澜起（津逮）** | 兆芯/海光拥有 x86 微架构设计/继承能力；澜起是 Intel Xeon 衍生 |
| 日本 | — | NEC V20/V30 已退出 |
| 欧洲 | — | Siemens/Infineon、STMicroelectronics 已退出 |
| 韩国 | — | Samsung 只是代工厂，不做 x86 CPU |
| 印度 | — | 无 x86 CPU 设计公司 |
| 俄罗斯 | — | Elbrus 通过二进制翻译兼容 x86，不是 x86 芯片 |
| 以色列 | — | Intel/AMD 设有设计中心，但不是独立 x86 公司 |

---

## 5. 容易混淆的“不是 x86 芯片公司”

### 5.1 代工厂

TSMC、GlobalFoundries、SMIC、UMC、Samsung Foundry、Intel Foundry——它们**制造** x86 芯片，但不设计 x86 CPU（Intel 除外，Intel 是 IDM）。

### 5.2 板卡/模块厂商

Advantech（研华）、Kontron、Congatec、Aaeon、Portwell、ICOP、WinSystems、VersaLogic、Diamond Systems、Eurotech、ADLINK、DFI、Arbor、IEI、Nexcom、Axiomtek、MSC、TQ、SECO 等。

它们把 Intel/AMD/VIA/DM&P/RDC 的 x86 SoC 做成主板、模块、工控机，**不是 CPU 设计公司**。

### 5.3 模拟器/翻译层

QEMU、Bochs、Unicorn、v86、Box64、FEX-Emu；Microsoft/Apple/Qualcomm 在 ARM 上做 x86 模拟/翻译。它们不是芯片公司。

### 5.4 其他指令集

- 龙芯 Loongson：LoongArch，不是 x86。
- 飞腾 Phytium、华为鲲鹏 Kunpeng：ARM，不是 x86。
- 申威 Sunway：SW64，不是 x86。
- RISC-V 厂商（SiFive、Ventana、平头哥、进迭时空等）：不是 x86。
- 俄罗斯 Elbrus：VLIW + x86 二进制翻译，不是 x86 ISA 芯片。

### 5.5 “x86 IP”公司

**基本不存在公开的独立 x86 CPU IP 供应商**（不像 ARM 那样可以买 IP 授权）。x86 IP 基本控制在 Intel/AMD/VIA 手中，这也是新公司难以进入的根本原因之一。

---

## 6. 结论

1. **x86 是双寡头 + 少数授权/兼容厂商的格局**：Intel、AMD 占绝对主导；VIA/兆芯/海光通过授权或兼容路线存在；DM&P/RDC 在工业长尾；澜起（津逮）是 Intel Xeon 衍生。
2. **中国大陆是唯一在 x86 上持续投入的非美地区**：兆芯（VIA 谱系）和海光（AMD Zen 授权）是仅有的两家通用 x86 CPU 设计公司；澜起（津逮）是 Intel Xeon 平台上的国产化/安全增强产品，不算自研 x86 核。
3. **嵌入式 x86 靠“兼容 + 长生命周期”生存**：DM&P、RDC 的性能不高，但在 DOS/Win7/工业接口/供货周期上有价值。
4. **x86 没有 RISC-V 式的新创生态**：ISA 授权、专利、微码版权、软件生态、验证成本共同构成壁垒。
5. **对 X86-KMH 的启示**：
   - 竞争/参考对象就是 Intel、AMD；兆芯/海光是最接近的“非美 x86 设计力量”。
   - 公开资料中没有 x86-64 乱序开源核，和上一份调研的结论一致。
   - 若考虑产品化，x86 授权/专利/FTO 是绕不开的门槛；研究/教学用途则应以 Intel SDM/AMD APM 为准，做 clean-room 实现。

---

## 7. 参考链接

### 公司

- Intel：<https://www.intel.com>
- AMD：<https://www.amd.com>
- VIA：<https://www.viatech.com>
- 兆芯：<https://www.zhaoxin.com>
- 海光：<https://www.hygon.cn>
- DM&P / Vortex86：<https://www.vortex86.com>
- RDC：<https://www.rdc.com.tw>
- SiS：<https://www.sis.com.tw>

### 产品/数据

- 兆芯 KX-7000：<https://www.zhaoxin.com/prod_view.aspx?nid=3&typeid=593&id=2757>
- 兆芯 KH-50000：<https://www.zhaoxin.com/prod_view.aspx?nid=3&typeid=605&id=3601>
- 澜起津逮 CPU（中文）：<https://www.montage-tech.com/cn/Jintide_Platform/Jintide_CPU>
- 澜起津逮 CPU（英文）：<https://www.montage-tech.com/Jintide_Platform/Jintide_CPU>
- 津逮 C6548N 规格：<https://www.montage-tech.com/cn/Jintide_Platform/Jintide_CPU/M88JTMC6548N>
- Hot Chips 31 Jintide 报道：<https://www.anandtech.com/show/14774/hot-chips-31-live-blogs-inteltsinghua-xeon-jintide-security-cpu>
- 海光 C86-5G 报道：<https://www.163.com/dy/article/L08T7APD0511BLFD.html>
- Vortex86DX3：<https://www.vortex86.com/products/Vortex86DX3>
- Vortex86EX2：<https://www.vortex86.com/products/Vortex86EX2>
- RDC 64-bit x86：<https://www.rdc.com.tw/en/news/64bitx86news>
- SiS 里程碑（1999 收购 Rise）：<https://www.sis.com.tw/en-us/Company/Milestones>
- Intel-VIA 2003 和解：<https://www.intel.com/pressroom/archive/releases/2003/20030407corp.htm>
- Hygon/AMD 2016 授权：<https://www.top500.org/news/china-begins-domestic-production-of-amd-server-cpus/>
- x86 市场份额（2026 Q2）：<https://www.techpowerup.com/351908/amd-gains-ground-as-intel-x86-market-share-goes-back-to-1995-levels>

### 历史厂商

- Cyrix：<https://en.wikipedia.org/wiki/Cyrix>
- NexGen：<https://en.wikipedia.org/wiki/NexGen>
- Rise Technology：<https://en.wikipedia.org/wiki/Rise_Technology>
- Centaur Technology：<https://en.wikipedia.org/wiki/Centaur_Technology>
- Transmeta：<https://en.wikipedia.org/wiki/Transmeta>
- IBM 386SLC：<https://en.wikipedia.org/wiki/IBM_386SLC>
- NEC V20：<https://en.wikipedia.org/wiki/NEC_V20>
- Geode：<https://en.wikipedia.org/wiki/Geode_(processor)>

---

*本文为调研记录，不构成法律意见；授权/专利用语以各公司官方披露和专业法务意见为准。*
