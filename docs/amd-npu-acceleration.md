# AMD NPU 加速技术方案（预研）

状态：Phase 0 无硬件预研。所有结论来自 AMD 官方文档与社区证据，**无真机数据，未立项开发**；本阶段只回答"AMD NPU 路线值不值得投、无硬件能推进到哪一步、真机需要验证什么"。

更新时间：2026-09-29

范围：AMD Ryzen AI NPU（XDNA/XDNA2：Phoenix、Hawk Point、Strix、Strix Halo、Krackan Point）。AMD GPU（iGPU/dGPU）继续走已发布的 Native WebGPU 主线，不在本方案内。关联文档：[Linux Device 加速技术方案 §6](linux-device-acceleration.md#6-厂商-gpunpu-路线)、[Intel NPU 加速技术方案](intel-npu-acceleration.md)（Direct OpenVINO 后端，本文多处对照）、[D111](decisions.md#d111--freeze-a-provider-neutral-execution-contract-before-enabling-accelerators)、[D112](decisions.md#d112--use-platform-aware-auto-with-creation-time-ordered-fallback)、[D113](decisions.md#d113--derive-locked-fp16-artifacts-for-the-webgpu-provider)（派生物规则）。

## 1. 结论

AMD NPU 与 Intel NPU 是**两条不同性质的路线**，不能把 Intel 方案直接平移：

- **官方推理栈以 Windows 为第一平台。** ORT 官方文档中 Vitis AI EP 对 Ryzen AI 的支持平台只列 Windows（Linux 仅覆盖 Zynq/Versal 等 Arm SoC）；Ryzen AI Software 从 1.7.1 起提供 Linux installer，但 Linux 属于后补平台，社区在 ORT 1.20.1 Linux 上仍遇到 voe 模块缺失、无法 offload 到 NPU 的问题（RyzenAI-SW #341）。**若坚持 Linux x64 优先，AMD NPU 的生态成熟度显著低于 Intel NPU（OpenVINO Linux 一等公民）。**
- **模型不能直接跑，必须量化派生。** Vitis AI EP 只接受 INT8 或 BF16 输入模型：Phoenix/Hawk Point 仅支持 CNN INT8；Strix/Krackan Point 支持 INT8 与 BF16。现有 locked FP32 ONNX 需要经过 Quark/Vitis AI Quantizer 量化，属于 D113 意义上的新 immutable 派生物，且 INT8 需要校准与独立质量 Gate。Intel NPU 的"直接读上游 ONNX + FP16 编译器默认"在这里不成立。
- **模型编译（NPU 微码生成）在 Linux 上不支持。** Ryzen AI Software 1.8.0 明确"Model generation is not supported on Linux in this release; models generated on Windows are compatible with Linux"——编译产物需要一个 Windows 构建步骤，派生物流水线必须为此设计（类似 Intel 方案排除的"离线 IR 派生物"，但在 AMD 这边是强制项）。
- **更贴近现有 WebGPU 路线而非 Intel Direct 路线。** Vitis AI EP 是 ONNX Runtime 的动态库 provider（模型在 session 创建时编译为微码，首次可能耗时数分钟，之后缓存），与现有 `OnnxSession` + dlopen provider 的结构同构；复用 WebGPU 的 provider 注册框架与 D113 派生物流程，比新建 Direct 后端成本低。
- **无硬件可以推进到"除了真机证据之外的一切"**：契约层、量化派生、Windows 编译产物锁定、栈审计、CI 失败路径（§8）。Spike 与 Gate 必须真机，无模拟器可用（§9）。
- **优先级判断：AMD 平台的"加速"诉求应先由 WebGPU 满足。** Strix iGPU 走 Mesa RADV/Vulkan 属于现有开放兼容路径，补 Provider Gate 即可；NPU 的独立收益（释放 CPU/GPU、省电）在真机 Spike 证实之前不构成开发理由。

## 2. 目标与非目标（预研阶段）

### 2.1 目标

1. 确认 AMD NPU 路线的技术可行性与前置条件，产出立项决策所需的证据清单。
2. 明确无硬件阶段可交付物，并尽可能在无硬件环境下完成。
3. 给出真机 Spike 的最小设备矩阵与验证项。

### 2.2 非目标

- 本阶段不写后端代码，不接入 runtime descriptor，不改动 Auto policy。
- 不评估 AMD GPU 的 WebGPU Provider Gate（另行按现有流程跑）。
- 不覆盖 Xilinx SoC（Zynq/Versal）——与本项目无 Tier 1 交集。

## 3. 已确认事实（官方文档与社区证据）

| 事实 | 来源 |
| --- | --- |
| Vitis AI EP 官方支持矩阵：Ryzen AI 仅 Windows；Linux 仅 Arm SoC | ORT 官方 EP 文档 Requirements 表 |
| Ryzen AI Software 1.8.0 支持 PHX/HPT/STX/STX Halo/KRK；Linux installer 自 1.7.1 提供 | 官方 Release Notes |
| 模型兼容矩阵：CNN INT8 全系支持；CNN/NLP BF16 仅 STX/KRK | 官方 Release Notes |
| 模型生成（编译）仅支持 Windows；Windows 产物可跑在 Linux | 官方 Release Notes Known Issues |
| EP 在 session 创建时把图与权重编译为微码可执行文件，首次编译可能数分钟，产物可缓存 | ORT 官方 EP 文档 |
| 量化工具：AMD Quark（ONNX/PyTorch）、Vitis AI Quantizer（提供 Docker：amdih/ryzen-ai-pytorch 等）、Olive（实验） | ORT 官方 EP 文档 |
| Linux 内核驱动 `amdxdna` 已进 mainline 6.14（Ubuntu 25.04 起），但 XRT SHIM 用户态库仍需自行构建 | amd/xdna-driver README、kernel docs |
| Linux 上 Vitis AI EP 存在可用性问题（voe 模块缺失、无法 offload） | RyzenAI-SW issue #341（2026-02） |
| Windows NPU production driver 32.0.203.376；安装依赖 VS2022/conda | 官方安装文档 |
| Linux NPU 生态的成熟用例是 FastFlowLM（LLM），与 CNN OCR 无关 | Frame.work 社区指南 |

尚未核实、需要真机或进一步调研的事项在 §11 列出。

## 4. 与 Intel NPU 路线的关键差异

| 维度 | Intel NPU（已实现 Phase B） | AMD NPU（本预研） |
| --- | --- | --- |
| 软件栈 | OpenVINO C API，Linux x64 一等公民 | ORT Vitis AI EP；Windows 一等，Linux 后补 |
| 后端形态 | 新 Direct 后端（`OpenVinoSession`） | 大概率复用 `OnnxSession` + provider 插件（WebGPU 模式） |
| 模型输入 | 上游 FP32 ONNX 直接读 | 必须量化派生（INT8/BF16） |
| 精度 | FP16（编译器默认） | INT8（全系）/ BF16（仅 STX/KRK） |
| 编译 | 运行时按 shape 编译，0.7–2 s，磁盘缓存 | session 创建时编译微码，可能数分钟，产物缓存 |
| 编译平台 | Linux 本机 | **仅 Windows**，产物跨平台使用 |
| 静态 shape | 桶 + 实际 shape 两种路由 | 待验证（EP 对动态 shape 的处理未知） |
| 驱动 | `intel_vpu` + Level Zero，随系统 | `amdxdna` mainline 6.14+，XRT SHIM 需自建 |

## 5. 设备与精度矩阵

| 设备 | XDNA 代 | CNN INT8 | CNN BF16 | 备注 |
| --- | --- | :-: | :-: | --- |
| Ryzen 7040 / 8040（PHX/HPT） | XDNA 1 | ✅ | ❌ | 只能走 INT8，量化质量风险最高 |
| Ryzen AI 300 / Krackan（STX/KRK） | XDNA 2 | ✅ | ✅ | BF16 绕开 INT8 校准，预研首选 |
| Ryzen AI Max（Strix Halo） | XDNA 2 | ✅ | ✅ | 台式/迷你主机形态，便于 Gate |

真机优先级：STX 系（BF16 可用、量化路径最短）> PHX/HPT（验证 INT8 路线是否值得）。

## 6. 候选架构

### 6.1 方案 A：ORT Vitis AI EP（倾向）

- 沿用 `OnnxSession` 框架：dlopen provider 库、注册 EP、provider options 传入量化模型路径与编译缓存目录。
- provider 名称建议 `amdnpu`，与 `webgpu`/`openvino` 并列；qualification-only policy、D112 原因映射照抄现有模式。
- 优点：与 WebGPU 路线同构，改造点集中在"量化派生模型加载 + provider options + 编译缓存管理"。
- 风险：EP 对 ORT 版本的绑定（社区踩坑在 ORT 1.20.1，本项目锁 1.22，兼容性未知）；Linux 分发形态（pip/conda 还是独立 .so）待审计。

### 6.2 方案 B：等待 OpenVINO 系 AMD NPU 支持

- 若 OpenVINO 生态出现成熟的 AMD NPU plugin，则可与 `OpenVinoSession` 合流，仅换 device 与精度契约。
- 现状：OpenVINO 主线的 NPU plugin 面向 Intel；AMD 侧无官方对应物。**仅作观察项，不作为计划。**

### 6.3 否决项（预研即排除）

- **整机模拟**：NPU 固件不开源，无 QEMU 设备模型；EP 的 NPU 编译器 target 真硬件，CPU fallback 不经过 NPU 代码路径——"无硬件验证推理行为"不存在。
- **FastFlowLM**：面向 transformer LLM，不覆盖 CNN OCR。
- **先做 PHX/HPT INT8**：量化派生 + 校准 + 质量 Gate 三重前置成本，若无 STX 真机数据支撑收益，投入产出比最差。

## 7. 模型派生（最大工作量，无硬件可完成大半）

按 D113 的派生物规则设计，所有产物可复现、锁定 SHA-256、进入 bundle manifest：

1. **量化（CPU 上可做）**：AMD Quark 或 Vitis AI Quantizer（官方 Docker 镜像），输入上游 FP32 ONNX，输出 INT8 或 BF16 ONNX。需要：
   - 校准集：复用 14-fixture locked corpus 的代表性样本；
   - INT8 需要精度预算与逐层审查（D113 未覆盖的新维度：量化误差审计）。
2. **NPU 编译（Windows 步骤）**：在 Windows 环境（本地或 CI runner）用 Ryzen AI 工具链把量化模型编译为 NPU 微码产物；产物锁定哈希后随包分发，运行时不再编译。
   - 这直接消除"首次编译数分钟"的运行时成本，比 Intel 路线（运行时编译 + 磁盘缓存）更可控；
   - 代价是派生物流程新增一个 Windows 构建环节，需要 CI 化与 provenance 记录。
3. **形状契约**：recognition 宽度桶复用现有 20 桶契约（是否被 EP 支持待验证）；detection 实际 shape 路由在 AMD 上如何表现（编译产物是否可按 shape 集合预生成）是 Spike 必答项。

## 8. 无硬件阶段可交付物（Phase 0 清单）

| 交付物 | 验证方式 |
| --- | --- |
| ✅ `ExecutionProvider::amdnpu` 契约层（枚举、校验、policy、CLI/Node/TS 类型） | 已落地：单测覆盖精度契约（仅 `auto`）、无 bundled provider 的 `unsupported_capability`、`provider_abi_mismatch`、Auto 下的 fail-fast |
| EP 装载 plumbing（dlopen、符号、注册） | 安装 Ryzen AI Software 后在无 NPU 主机上跑到"设备枚举失败"，覆盖 `adapter_unavailable` |
| INT8/BF16 量化派生流水线 | CPU Docker 内对 locked 模型跑通，产物与哈希入库 |
| Windows 编译步骤 CI 化 | GitHub Windows runner 生成微码产物 + provenance |
| 软件栈审计报告 | 包体、依赖闭包、许可证/SBOM、ORT 版本矩阵、XRT SHIM 分发形态 |
| ✅ 更新 [linux-device-acceleration.md §6](linux-device-acceleration.md#6-厂商-gpunpu-路线) AMD NPU 行链接本文 | 文档评审 |

## 9. 真机前置（Phase A Spike 必答项）

需要一台 STX 设备（Ryzen AI 300 或 Strix Halo 系）：

1. det/rec 单模型 microbenchmark：BF16 与 INT8 各自的 P50、首次编译/缓存命中行为、与 CPU 基线倍数。
2. 14-fixture 完整 OCR：文本一致率、置信度偏差（对照 CPU FP32 goldens）；INT8 的行级差异审计。
3. EP 动态 shape 行为：20 桶 + detection 实际 shape 是否可行，还是需要全静态 shape 集合。
4. 生命周期与缓存：20 次 engine close/recreate 的 RSS/句柄；微码缓存的跨进程行为。
5. Linux 可用性专项：XRT SHIM 构建、EP 注册、issue #341 类故障是否复现；若 Linux 不通，评估 Windows x64 作为第一平台（与 Intel 路线的 Linux 优先相反）。

## 10. 分阶段落地（草案）

- **Phase 0（本预研 → 可立即开工）**：完成 §8 清单；产出立项决策材料。
- **Phase A（真机 Spike）**：借/购 STX 设备，完成 §9；数据不达预期则止损归档。
- **Phase B（后端实现）**：按 §6.1 实现，qualification-only；仅当 Phase A 通过。
- **Phase C/D（Gate 与分发）**：结构与 Intel NPU 方案 §11 相同，增加 INT8 质量审计与 Windows 编译产物锁定。
- **进入 Auto**：按 D112 记录新 D 编号后才进入 released policy。

## 11. 待决问题

1. Linux 是否继续作为第一平台，还是 AMD 路线以 Windows x64 起步？
2. BF16（仅 STX+）与 INT8（全系但需校准）如何取舍：双精度派生还是单精度？
3. Vitis AI EP 与本项目锁定的 ORT 1.22 是否兼容；EP 库以何种形态随包分发（pip/conda/NuGet 之外的 .so 提取）？
4. 微码编译产物是否与 NPU 驱动版本耦合（驱动升级是否使产物失效）？
5. XRT SHIM 在无发行版包的现状下，是否视为驱动栈前置条件随包分发？
6. PHX/HPT（仅 INT8）是否纳入 Gate 矩阵，还是明确不支持？
7. 真机来源：购置 STX 笔记本/迷你主机，或租用云上 Ryzen AI 主机？

## 12. 参考

- [Vitis AI Execution Provider（ORT 官方文档）](https://onnxruntime.ai/docs/execution-providers/Vitis-AI-ExecutionProvider.html)
- [Ryzen AI Software 1.8.0 Release Notes](https://ryzenai.docs.amd.com/en/latest/relnotes.html)
- [Ryzen AI 安装文档（Windows 主线）](https://ryzenai.docs.amd.com/en/latest/inst.html)
- [AMD XDNA Driver for Linux](https://github.com/amd/xdna-driver)
- [Linux amdxdna 内核文档](https://docs.kernel.org/accel/amdxdna/index.html)
- [AMD Quark 量化工具](https://quark.docs.amd.com/latest/supported_accelerators/ryzenai/index.html)
- [RyzenAI-SW Linux EP issue #341](https://github.com/amd/RyzenAI-SW/issues/341)
