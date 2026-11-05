# 全球 AI 芯片公司地图（GPU / NPU / TPU / ASIC）

> 调研日期：2026-11-05  
> 范围：研发 GPU、NPU、TPU、AI ASIC、AI 加速器、AI 芯片 IP 的公司  
> 说明：“AI 芯片”没有统一口径；本文按“研发/计划制造”“已量产”“数据中心”“边缘/终端”“IP”分开统计。核心数据来自 Jon Peddie Research（JPR）2026 Q1、IDC、Morgan Stanley、公司公告与公开报道。

## 0. 结论

**JPR 2026 Q1：全球共 135 家公司正在研发或计划制造 AI 处理器**——其中 36 家上市公司、99 家初创公司；累计投入超过 288 亿美元；JPR 预计到 2030 年将收缩到约 25 家。

如果换口径：

| 口径 | 大约家数 |
| --- | ---: |
| 研发/计划制造 AI 处理器（JPR） | **135**（36 public + 99 startups） |
| 已量产、有实际出货的 AI 芯片公司 | 约 40–60 |
| 数据中心 AI 加速器（GPU/ASIC/TPU） | 约 30–40 |
| 边缘/终端 NPU（含 IP） | 约 50–80 |
| AI GPU（含集成 GPU/IP） | 约 20–30 |
| AI 芯片 IP 供应商 | 约 20–30 |
| 汽车 AI 芯片 | 约 20–30 |
| 中国大陆 AI 芯片公司（含初创/IP） | 约 50–100，其中有存在感的 20–30 |

> 一句话：**“研发 AI 芯片”约 135 家；“真正量产并形成收入”约 40–60 家；“数据中心 AI 加速器”约 30–40 家；NVIDIA 仍占约 85–90% 的 AI 芯片收入。**

## 1. 口径与分类

AI 芯片不是单一品类，至少包括：

| 类别 | 说明 | 代表 |
| --- | --- | --- |
| **GPU / GPGPU** | 通用并行处理器，训练/推理 | NVIDIA、AMD、Intel、Moore Threads、Biren、MetaX、Iluvatar、Tianshu |
| **TPU / AI ASIC** | 为特定 AI 负载定制的加速器 | Google TPU、AWS Trainium/Inferentia、Microsoft Maia、Meta MTIA、OpenAI/Broadcom、华为昇腾、寒武纪、燧原 |
| **NPU** | 神经网络处理单元，手机/PC/边缘/汽车 | Apple Neural Engine、Qualcomm Hexagon、MediaTek APU、Arm Ethos、Hailo、Axelera、Kneron |
| **AI IP** | 只卖 IP，不卖芯片 | Arm、Imagination、VeriSilicon、Cadence、Synopsys、Ceva、Andes、Expedera |
| **CIM / 存算一体 / 神经形态** | 新架构 | Mythic、Rain AI、IBM?、Syntiant、Innatera |
| **AI 芯片设计服务** | 为客户定制 ASIC | Broadcom、Marvell、Alchip、GUC、Faraday、Socionext |

> 同一家公司可能跨多个类别，所以“分类家数”相加会大于 135。

## 2. 主要玩家

### 2.1 GPU / GPGPU

| 公司 | 说明 |
| --- | --- |
| **NVIDIA** | H100/H200/Blackwell/Blackwell Ultra/Vera Rubin；AI 训练/推理绝对龙头 |
| **AMD** | Instinct MI300/MI350/MI400；ROCm 生态 |
| **Intel** | Gaudi 3、数据中心 GPU、集成 GPU；AI 加速器业务调整中 |
| **Apple** | M 系列 GPU + Neural Engine；自用 |
| **Qualcomm** | Adreno GPU + Hexagon NPU；手机/PC/汽车 |
| **Arm / Imagination** | Mali/Immortalis、PowerVR GPU IP |
| **Moore Threads（摩尔线程）** | MTT S 系列 GPGPU；中国 |
| **Biren（壁仞）** | BR100/BR104 等 GPGPU；中国 |
| **MetaX（沐曦）** | 曦云/曦索系列 GPGPU；中国 |
| **Iluvatar（天数智芯）** | 天垓/智铠 GPGPU；中国 |
| **Tianshu（燧原）** | 云端 AI ASIC（非 GPGPU）；中国 |
| **Jingjia Micro（景嘉微）** | 国产 GPU；中国 |
| **Loongson（龙芯）** | 集成 GPU；中国 |
| **Innosilicon / VeriSilicon** | GPU/AI IP 与定制芯片 |

### 2.2 数据中心 TPU / AI ASIC

| 公司 | 说明 |
| --- | --- |
| **Google** | TPU 系列；2026 年出货目标上调至约 600 万颗；Anthropic 等大客户 |
| **AWS** | Trainium 3 / Inferentia；Project Rainier、Anthropic 大规模部署 |
| **Microsoft** | Maia 200；Azure 自用 |
| **Meta** | MTIA 系列；自用 |
| **OpenAI + Broadcom** | 定制 AI ASIC（据报道，10GW 级） |
| **ByteDance（字节跳动）** | 定制 AI 芯片（据报道） |
| **Alibaba（阿里）** | 含光 Hanguang 800 等 |
| **Baidu（百度）** | 昆仑 Kunlun 系列 |
| **Huawei（华为）** | 昇腾 Ascend 910/950 系列 |
| **Cambricon（寒武纪）** | 思元 MLU 系列 |
| **Enflame（燧原）** | 云端训练/推理 ASIC |
| **Biren / Moore Threads / MetaX / Iluvatar / Tianshu / Hango** | 中国云端 AI 芯片 |
| **Broadcom / Marvell / Alchip / GUC** | 为云厂商定制 AI ASIC 的设计服务 |

### 2.3 边缘/终端 NPU

| 公司 | 说明 |
| --- | --- |
| **Apple** | Neural Engine；iPhone/iPad/Mac |
| **Qualcomm** | Hexagon NPU；手机/PC/汽车 |
| **MediaTek** | APU；手机/平板/Chromebook |
| **Samsung** | Exynos NPU |
| **Google** | Tensor NPU / Edge TPU |
| **Huawei** | 昇腾/达芬奇 NPU |
| **UNISOC / Xiaomi / Rockchip / Amlogic / Actions** | 手机/平板/边缘 NPU |
| **Arm** | Ethos-N/U NPU IP |
| **Cadence / Synopsys / Ceva / VeriSilicon / Imagination / Andes / Expedera** | NPU IP |
| **Hailo / Axelera / Kneron / Sima.ai / GreenWaves** | 边缘 AI 芯片初创 |

### 2.4 汽车 AI

| 公司 | 说明 |
| --- | --- |
| **NVIDIA** | Orin/Thor |
| **Qualcomm** | Snapdragon Ride |
| **Mobileye** | EyeQ |
| **Horizon Robotics（地平线）** | 征程 Journey |
| **Black Sesame（黑芝麻）** | 华山 Huashan |
| **NXP / Renesas / TI / Infineon** | 汽车 SoC/MCU + AI |
| **Ambarella / Telechips** | 视觉/座舱 AI |

### 2.5 初创公司（JPR 99 家中的代表）

- **Cerebras**：晶圆级 AI 芯片
- **Groq**：LPU 推理芯片；2025-12 与 NVIDIA 达成非独家许可
- **SambaNova**：RDU；与 Intel 合作
- **Tenstorrent**：RISC-V + AI 加速器
- **Graphcore**：IPU（已被 SoftBank 收购）
- **Esperanto**：RISC-V AI 加速器
- **d-Matrix / Etched / Lightmatter / Rain AI / Mythic / Syntiant / Innatera**：推理、Transformer ASIC、光计算、存算一体、神经形态等

## 3. 市场格局

| 指标 | 数据 |
| --- | --- |
| AI 处理器市场规模 | JPR：约 **4930 亿美元**（2026 Q1 追踪） |
| NVIDIA AI 芯片收入份额 | 约 **85–90%**（Morgan Stanley 2026-03：约 85%；另有报道约 90%） |
| 定制 ASIC 份额 | **>10%** |
| AMD 份额 | **<5%** |
| AI 芯片公司数量 | **135 家**（36 public + 99 startups） |
| 2030 年预计存活 | **约 25 家**（JPR） |
| AI ASIC 市场 | 2023 年 66 亿美元 → 2028 年 554 亿美元（Marvell 预测，CAGR 53%） |
| 中国 AI 加速卡 2025 | 约 **400 万张**；NVIDIA 约 55%，国产约 41%（IDC） |

要点：

- **NVIDIA 仍是绝对龙头**，但云厂商自研 ASIC（Google TPU、AWS Trainium、Microsoft Maia、Meta MTIA）正在形成“第二供应链”。
- **AI ASIC 增速高于通用 GPU**，Broadcom/Marvell/Alchip/GUC 等设计服务受益。
- **初创公司进入“斩杀线”**：JPR 预计 2030 年仅约 25 家专业 AI 芯片公司存活；资本向头部集中，云厂商自研挤压初创。
- **中国是唯一能形成完整替代链的非美地区**：华为昇腾、寒武纪、壁仞、摩尔线程、沐曦、天数智芯、燧原、瀚博等；2025 年国产 AI 加速卡份额约 41%。

## 4. 中国 AI 芯片公司

### 4.1 “国产 AI 芯片六小龙”

| 公司 | 路线 |
| --- | --- |
| **摩尔线程** | 全功能 GPGPU |
| **沐曦** | GPGPU（曦云/曦索） |
| **壁仞** | GPGPU |
| **天数智芯** | GPGPU（天垓/智铠） |
| **燧原** | 云端 AI ASIC（非 GPGPU） |
| **瀚博** | 云端 AI 推理 + 渲染 |

### 4.2 其他重要玩家

- **华为海思**：昇腾 Ascend
- **寒武纪**：思元 MLU
- **百度**：昆仑 Kunlun
- **阿里**：含光 Hanguang
- **地平线 / 黑芝麻**：汽车 AI
- **景嘉微 / 龙芯**：GPU
- **燧原 / 壁仞 / 摩尔线程 / 沐曦 / 天数 / 瀚博**：云端 AI

> 中国 AI 芯片公司总数（含初创/IP）约 50–100 家；其中有量产产品、有收入的约 20–30 家。

## 5. 为什么没有精确数字

- **“研发” vs “量产”**：JPR 的 135 家包括“正在研发或计划制造”，很多初创尚未量产。
- **“芯片” vs “IP”**：Arm、Imagination、VeriSilicon、Cadence、Synopsys 等卖 IP，不卖芯片。
- **“GPU/NPU/TPU/ASIC”边界模糊**：云厂商的 ASIC 可能叫 TPU、NPU、DPU、XPU。
- **云厂商自用 vs 对外卖**：Google/AWS/Microsoft/Meta 主要自用，不单独卖芯片。
- **初创/ stealth**：很多公司未公开；中国还有大量未上市/未披露的 AI 芯片公司。
- **并购/退出**：Graphcore 被 SoftBank 收购、Groq 与 NVIDIA 许可、Intel 与 SambaNova 合作等，名单每年变化。

## 6. 结论

1. **全球约 135 家公司研发 AI 处理器**（36 上市 + 99 初创）；到 2030 年预计只剩约 25 家。
2. **真正量产、有收入的约 40–60 家**；数据中心 AI 加速器约 30–40 家；边缘 NPU 约 50–80 家；AI GPU 约 20–30 家。
3. **NVIDIA 占 AI 芯片收入约 85–90%**；定制 ASIC >10%，AMD <5%。
4. **云厂商自研 ASIC 是最大变量**：Google TPU、AWS Trainium、Microsoft Maia、Meta MTIA、OpenAI/Broadcom。
5. **中国是唯一形成完整替代链的非美地区**：华为昇腾、寒武纪、壁仞、摩尔线程、沐曦、天数、燧原、瀚博等；2025 年国产 AI 加速卡份额约 41%。

## 7. 参考链接

### 数据 / 报告

- JPR Q1'26：135 家 AI 处理器公司：<https://www.jonpeddie.com/news/jon-peddie-research-releases-its-q126-ai-processors-quarterly-update-series-report/>
- 36kr/C114：AI 芯片初创公司进入“斩杀线”：<https://www.c114.net.cn/industry/76986.html>
- AIMultiple：Top 30+ AI Chip Makers：<https://aimultiple.com/ai-chip-makers>
- Epoch AI：AI chip market share by designer：<https://epoch.ai/graphs/ai-chip-market-share-by-designer>
- TrendForce：Google TPU race（Marvell/AMD vs Broadcom/MediaTek）：<https://www.trendforce.com/news/2026/08/20/news-marvell-amd-reportedly-shake-up-google-tpu-race-putting-broadcom-MediaTek-under-pressure>
- 国产 AI 芯片六小龙：<https://www.ceweekly.cn/economic/industry/2026/0214/489412.html>

### 公司

- NVIDIA：<https://www.nvidia.com>
- AMD：<https://www.amd.com>
- Intel：<https://www.intel.com>
- Google Cloud TPU：<https://cloud.google.com/tpu>
- AWS Trainium：<https://aws.amazon.com/machine-learning/trainium/>
- Microsoft Maia：<https://azure.microsoft.com>
- Meta MTIA：<https://ai.meta.com>
- Broadcom：<https://www.broadcom.com>
- Marvell：<https://www.marvell.com>
- 华为昇腾：<https://www.hisilicon.com>
- 寒武纪：<https://www.cambricon.com>
- 壁仞：<https://www.birentech.com>
- 摩尔线程：<https://www.mthreads.com>
- 沐曦：<https://www.metax-tech.com>
- 天数智芯：<https://www.iluvatar.com>
- 燧原：<https://www.enflame-tech.com>
- 瀚博：<https://www.vastaitech.com>
- 地平线：<https://www.horizon.cc>
- 黑芝麻：<https://www.blacksesame.com.cn>
- Cerebras：<https://www.cerebras.ai>
- Groq：<https://groq.com>
- SambaNova：<https://sambanova.ai>
- Tenstorrent：<https://tenstorrent.com>
- Graphcore：<https://www.graphcore.ai>
- Hailo：<https://hailo.ai>
- Axelera：<https://axelera.ai>

---

*本文为调研记录；“家数”为按公开信息的估算，不是官方统计。*
