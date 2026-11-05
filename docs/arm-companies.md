# 全球 ARM CPU / SoC 公司地图

> 调研日期：2026-11-05  
> 范围：销售或设计 ARM 架构 CPU / SoC / MCU 的公司  
> 说明：“卖 ARM CPU”没有统一口径；本文按“芯片设计商、merchant 芯片、 captive 自用、云厂商、板卡厂商”分开统计。数据来源见文末。

## 0. 结论：没有唯一数字

| 口径 | 大约家数 | 说明 |
| --- | ---: | --- |
| ARM IP 授权（主要不卖芯片） | 1 | Arm；Arm China 是授权/服务实体；2026 年起 Arm 也卖 AGI CPU |
| 卖 ARM 服务器 CPU | 10–12 | Arm、Ampere、AWS、Google、Microsoft、阿里、华为、飞腾、富士通、NVIDIA、SiPearl、Qualcomm |
| 卖 ARM 手机/PC/平板 AP | ~10 | Qualcomm、MediaTek、Apple、Samsung、Google、华为、紫光展锐、小米、NVIDIA、Rockchip |
| 卖 ARM 嵌入式 AP/SoC | ~15–20 | NXP、Renesas、ST、TI、Microchip、Rockchip、Allwinner、Amlogic、Actions、Socionext、Telechips 等 |
| 卖 ARM MCU | ~30–40 | ST、NXP、Renesas、Infineon、Microchip、TI、Nordic、Silicon Labs、Nuvoton、GigaDevice 等 |
| 卖 ARM 板卡/模块 | 数百 | Raspberry Pi、Advantech、Kontron、Congatec 等 |
| **合计（芯片设计公司）** | **约 80–120** | 其中真正有存在感的约 30–40 家 |

> 一句话：**如果只算“卖 ARM 服务器 CPU”，大约 10–12 家；如果算“卖 ARM 芯片（含 MCU/SoC）”，约 80–120 家；如果算板卡/模块，数百家。**

## 1. 先厘清：Arm 主要卖 IP，但 2026 年开始自己卖芯片

- Arm 的商业模式是 IP 授权（Cortex、Neoverse、CSS、GPU、NPU 等），本身长期不卖 CPU 芯片。
- **2026 年 3 月，Arm 首次推出并销售量产自研芯片“Arm AGI CPU”**：最高 136 个 Neoverse V3 核心、约 300W，面向 AI 数据中心/智能体负载；Meta 是首个主要客户，Cerebras、Cloudflare、F5、OpenAI、Positron、Rebellions、SAP、SK Telecom 等将部署；中国客户包括联想、字节跳动火山引擎、新紫光集团。
- 这意味着 Arm 现在既是 IP 授权方，也是芯片销售方；统计“卖 ARM CPU 的公司”时，Arm 自己也要算进去（但它只有一个数据中心产品线，不是通用 CPU 厂商）。

## 2. 按类别的主要玩家

### 2.1 手机 / PC / 平板 AP

| 公司 | 说明 |
| --- | --- |
| **Qualcomm** | Snapdragon 8 系/7 系、Snapdragon X 系列 PC；ARM 手机/PC AP 龙头之一 |
| **MediaTek** | Dimensity 手机、Kompanio Chromebook/平板；出货量最大的手机 SoC 厂商之一 |
| **Apple** | A 系列/M 系列；自用，不单独卖芯片 |
| **Samsung** | Exynos；部分外卖，主要自用 |
| **Google** | Tensor；自用，与 Samsung/TSMC 合作制造 |
| **华为 / HiSilicon** | Kirin；自用，受美国制裁/先进制程限制影响 |
| **紫光展锐 UNISOC** | 手机/平板/物联网 SoC，外卖 |
| **小米** | 玄戒 XRING O1（2025）、O2（2026，台积电 N3P）；自用 |
| **NVIDIA** | Tegra/Orin/Thor（汽车/嵌入式）；据报道 PC 端 N1X 等 ARM SoC 在推进 |
| **Rockchip** | RK 系列，平板/Chromebook/嵌入式，外卖 |
| **Allwinner / Amlogic / Actions** | 平板、TV box、嵌入式，外卖 |

> 这一类的“卖”要分 merchant（Qualcomm、MediaTek、UNISOC、Rockchip 等）和 captive（Apple、Google、华为、小米等）；captive 不单独卖芯片，但设计 ARM CPU。

### 2.2 服务器 / 云 CPU

| 公司 | 模式 | 说明 |
| --- | --- | --- |
| **Arm** | merchant | 2026 年 AGI CPU，136×Neoverse V3，Meta 首发 |
| **Ampere** | merchant | 2025-11 被 SoftBank 完成收购；AmpereOne 系列 ARM 服务器 CPU |
| **AWS** | captive/cloud | Graviton；只卖云实例，不单独卖芯片 |
| **Google** | captive/cloud | Axion；只卖云实例 |
| **Microsoft** | captive/cloud | Cobalt；只卖云实例 |
| **阿里巴巴** | captive/cloud | 倚天 Yitian 710；主要用于阿里云 |
| **华为** | merchant/captive | 鲲鹏 Kunpeng；卖芯片/服务器 |
| **飞腾 Phytium** | merchant | FT 系列 ARM 服务器 CPU；中国 |
| **富士通 Fujitsu** | merchant | A64FX（富岳）、MONAKA（下一代） |
| **NVIDIA** | system | Grace/Grace Blackwell/Vera；主要随系统卖，不单卖 CPU |
| **SiPearl** | merchant | 欧洲 Rhea，ARM Neoverse |
| **Qualcomm** | merchant | 2026 年重返 ARM 服务器 CPU；收购 Alphawave（24 亿美元）补 SerDes |

> 如果只算“merchant 服务器 CPU”（单独卖芯片），大约 6–8 家：Arm、Ampere、华为、飞腾、富士通、SiPearl、Qualcomm，以及部分 NVIDIA 系统形态。AWS/Google/Microsoft/阿里是云厂商自用，不算“卖芯片”。

### 2.3 嵌入式 / 工业 AP / SoC

| 公司 | 说明 |
| --- | --- |
| **NXP** | i.MX、Layerscape；工业/汽车/网络 |
| **Renesas** | RZ、RA；工业/汽车 |
| **STMicroelectronics** | STM32MP；工业/嵌入式 |
| **Texas Instruments** | Sitara/AM 系列；工业/汽车 |
| **Microchip** | SAMA5/SAMA7；工业 |
| **Rockchip / Allwinner / Amlogic / Actions** | 平板、TV、嵌入式 |
| **Socionext** | 定制 SoC/ASIC |
| **Telechips** | 车载信息娱乐 |
| **Broadcom** | Raspberry Pi SoC、机顶盒/网络 |
| **Marvell** | Octeon/ARM 网络 SoC |
| **Ambarella** | ARM + AI 视觉 SoC |

### 2.4 MCU

| 公司 | 说明 |
| --- | --- |
| **STMicroelectronics** | STM32（Cortex-M） |
| **NXP** | LPC、Kinetis、i.MX RT |
| **Renesas** | RA、RL78?（RL78 非 ARM）、RX?（RX 非 ARM） |
| **Infineon** | PSoC、Traveo、XMC |
| **Microchip** | SAM 系列 |
| **Texas Instruments** | MSPM0?（MSPM0 是 ARM Cortex-M0+）、SimpleLink |
| **Nordic Semiconductor** | nRF52/53/54 |
| **Silicon Labs** | EFR32、EFM32 |
| **Nuvoton** | NuMicro |
| **GigaDevice** | GD32 |
| **Artery** | AT32 |
| **MindMotion** | MM32 |
| **HDSC** | HC32 |
| **Ambiq** | Apollo |
| **Toshiba** | TXZ |
| **Raspberry Pi** | RP2040/RP2350（ARM Cortex-M） |
| **Analog Devices** | ADuCM 等 |

> MCU 是 ARM 公司数量最多的领域；全球做 ARM Cortex-M 的厂商大约 30–40 家，中国厂商还在增加。

### 2.5 汽车

| 公司 | 说明 |
| --- | --- |
| **NXP** | S32 系列 |
| **Renesas** | R-Car、RH850?（RH850 非 ARM） |
| **Infineon** | Traveo、PSoC |
| **TI** | Jacinto |
| **Qualcomm** | Snapdragon Ride |
| **NVIDIA** | Orin/Thor |
| **Mobileye** | EyeQ（部分 ARM） |
| **Horizon Robotics** | 征程 Journey |
| **Black Sesame** | 华山 Huashan |
| **Telechips** | 车载信息娱乐 |

### 2.6 网络 / DPU

| 公司 | 说明 |
| --- | --- |
| **Broadcom** | 网络 SoC/交换芯片（含 ARM 核） |
| **Marvell** | Octeon 系列 |
| **NXP** | Layerscape |
| **Cisco** | Silicon One（部分含 ARM 核） |
| **Ampere** | 服务器 CPU，也可用于网络/边缘 |

### 2.7 ASIC / 设计服务

| 公司 | 说明 |
| --- | --- |
| **Faraday** | ARM-based ASIC |
| **Alchip** | 高复杂度 ASIC |
| **Global Unichip (GUC)** | ASIC |
| **Socionext** | 定制 SoC |
| **MegaChips** | ASIC |
| **VeriSilicon** | IP + ASIC |

> 这些公司不卖“ARM CPU 品牌芯片”，但会为客户做含 ARM 核的定制芯片；如果把它们算上，“卖 ARM 芯片”的公司数量会再增加几十家。

### 2.8 板卡 / 模块 / 整机

Raspberry Pi、Advantech（研华）、Kontron、Congatec、AAEON、Portwell、Toradex、Variscite、PHYTEC、SolidRun、Pine64、Hardkernel 等。

> 它们卖的是板卡/模块/整机，不是 CPU 芯片；数量数百家，不建议混入“芯片公司”统计。

## 3. 2026 年的关键变化

1. **Arm 自己下场卖芯片**：AGI CPU（136×Neoverse V3，约 300W）2026-03 发布，Meta 首发；中国客户联想、字节跳动火山引擎、新紫光。
2. **SoftBank 完成对 Ampere 的收购**（2025-11-25）：Ampere 成为 SoftBank 全资子公司，继续卖 ARM 服务器 CPU。
3. **Qualcomm 重返 ARM 服务器 CPU**：收购 Alphawave（24 亿美元）补 SerDes，Oryon CPU 瞄准数据中心。
4. **小米玄戒 O2**：2026 年台积电 N3P，ARM 自研 AP，手机厂商自研芯片继续扩散。
5. **Arm 服务器收入份额 >45%**：IDC 2026 Q1，Arm 服务器收入占比超过 45%，x86 约 52%；Arm 预计 2026 下半年/2027 超过 50%。按出货量 x86 仍高（约 2000 万颗/年）。Arm 的服务器客户数已达 7 万家，2021 年以来增长 14 倍。
6. **Arm PC/平板**：Arm 预计 2026 年占 PC/平板出货约 40%；Windows on ARM、Apple M 系列、Snapdragon X 系列共同推动。

## 4. 为什么“多少家”没有统一答案

- **Arm 不公布完整被授权方名单**，很多授权是保密的。
- **“卖芯片” vs “设计芯片”**：Apple、Google、华为、小米、AWS、Microsoft、阿里等主要是自用/云，不单独卖芯片。
- **“CPU” vs “SoC/MCU”**：MCU 厂商数量远多于服务器/PC CPU 厂商。
- **“merchant” vs “captive”**：Qualcomm/MediaTek 卖芯片；Apple/Google 卖设备；AWS/Google/Microsoft 卖云实例。
- **ASIC/设计服务**：Faraday、Alchip、GUC 等做含 ARM 核的定制芯片，但不算 ARM CPU 品牌。
- **并购/退出**：Ampere 被 SoftBank 收购、Centaur 被 Intel 收编、Calxeda/Applied Micro 等已消失。

## 5. 结论

1. **ARM 芯片公司数量远多于 x86**：x86 约 8 家，ARM 芯片设计公司约 80–120 家，核心 30–40 家。
2. **但高性能 ARM CPU 仍然集中**：服务器 10–12 家、手机/PC AP 约 10 家；大多数 ARM 公司是 MCU/嵌入式 SoC。
3. **ARM 的“开放授权”是数量多的根本原因**：Arm 卖 IP，芯片公司做 SoC/MCU；x86 只有 Intel/AMD/VIA 等极少数授权方。
4. **2026 年 Arm 自己开始卖芯片**，这是 ARM 生态的一个结构性变化；但它只切入数据中心，不是通用 CPU 市场。
5. **对 X86-KMH 的启示**：ARM 生态的广度来自开放授权与碎片化；x86 的壁垒是 ISA/专利/微码/生态，玩家数量天然少一个数量级。

## 6. 参考链接

### Arm 官方 / 产品

- Arm AGI CPU：<https://www.arm.com/products/cloud-datacenter/arm-agi-cpu>
- Arm AGI CPU 发布：<https://www.arm.com/company/news/2026/03/arm-agi-cpu-launch>
- Arm China：<https://www.armchina.com>

### 2026 年新闻 / 数据

- Arm AGI CPU 136 核报道：<https://www.163.com/dy/article/KOR7J72I05198CJN.html>
- Arm 数据中心中国客户（联想/火山引擎/新紫光）：<https://finance.eastmoney.com/a/202609083868115118.html>
- Arm 服务器客户 7 万家：<https://cloud.tencent.cn/developer/article/2641314>
- IDC：Arm 服务器收入份额 >45%：<https://m.eepw.com.cn/article/202606/482048.html>
- SoftBank 完成收购 Ampere：<https://group.softbank/en/news/press/20251126>
- Qualcomm 重返服务器 CPU：<https://www.lemagit.fr/actualites/366645745/Puces-AI-Qualcomm-detaille-ses-futurs-CPU-et-GPU-pour-serveurs>
- 小米玄戒 O2：<http://www.c114.net.cn/industry/53052.html>

### 公司

- Qualcomm：<https://www.qualcomm.com>
- MediaTek：<https://www.mediatek.com>
- Apple：<https://www.apple.com>
- Samsung Semiconductor：<https://semiconductor.samsung.com>
- Google Cloud Axion：<https://cloud.google.com/axion>
- AWS Graviton：<https://aws.amazon.com/ec2/graviton/>
- Microsoft Cobalt：<https://azure.microsoft.com>
- Ampere：<https://amperecomputing.com>
- NVIDIA Grace：<https://www.nvidia.com/en-us/data-center/grace-cpu/>
- 华为鲲鹏：<https://www.hisilicon.com>
- 飞腾：<https://www.phytium.com.cn>
- 富士通 A64FX：<https://www.fujitsu.com>
- SiPearl：<https://www.sipearl.com>
- NXP：<https://www.nxp.com>
- STMicroelectronics：<https://www.st.com>
- Renesas：<https://www.renesas.com>
- Infineon：<https://www.infineon.com>
- Microchip：<https://www.microchip.com>
- TI：<https://www.ti.com>
- Nordic：<https://www.nordicsemi.com>
- Silicon Labs：<https://www.silabs.com>
- Raspberry Pi：<https://www.raspberrypi.com>

---

*本文为调研记录；“多少家”为按公开信息的估算，不是 Arm 官方统计。*
