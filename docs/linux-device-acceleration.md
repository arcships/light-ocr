# Linux Device 加速技术方案

状态：Linux x64 glibc/Vulkan 与 Windows x64/D3D12 产品实现和真实设备 Provider Gate 已完成；两平台均为 164/164 Gate 通过，production lock 待绑定已审阅报告和产物哈希

更新时间：2026-07-19

范围：当前交付目标是 Linux x64 glibc；Linux arm64、移动端与边缘 NPU 属于后续平台决策

关联 Roadmap：[Perf-0–Perf-4](roadmap.md#7-perf-0perf-4--性能与宿主加速线)

## 1. 结论

Linux 加速可复用当前 ONNX 模型和 backend-neutral `InferenceSession` 边界，不需要重写 OCR pipeline。推荐把候选分成两层：

- **Native ONNX Runtime WebGPU EP 已选为 Linux x64 通用 GPU 主线。** 它通过 Dawn 使用 Vulkan，第一轮直接验证当前 FP32 ONNX detector/recognizer，不先制作 FP16 模型，也不先建设 GPU preprocess。
- **D112 Auto 的 Linux 初始顺序为 `webgpu → cpu`。** 只有创建期 Auto 可按封闭原因分类尝试下一候选；显式 `provider=webgpu|cpu` 只尝试指定 backend，运行期 inference failure 不切换。
- **厂商路径是专用后端。** 只有 WebGPU 未覆盖目标设备/workload，或 CUDA/OpenVINO/MIGraphX 的用户加权收益足够高时才启动，并独立通过 PG。NPU 没有 Linux 全平台统一 API。
- **CPU 是稳定最终候选和显式 backend。** WebGPU 不是 NPU API，也不是“任何机器都更快”的全平台兜底。
- **兼容层由上游实现，产品兼容由本项目负责。** ONNX Runtime WebGPU EP 提供算子 kernels，Dawn/Tint 映射 Vulkan/D3D12/Metal；`light-ocr` 仍负责模型覆盖、驱动矩阵、包体、质量、性能、资源和 D112 Auto 契约。

当前源码已完成 official plugin runtime、C++/Node provider 接线、D112 Auto、schema 2 runtime descriptor、自包含 npm staging、许可证/SBOM、离线复装、跨平台编译测试与双平台真实设备 Gate。显式 FP32 allow 证明 placement 与质量，strict 证明 bounded CPU partition 可以稳定 fail-closed，Auto 与直接 C++ 路径证明产品选择。当前 lock 仍保持 `development-pending-device-validation`，普通 release configure 和 npm release staging 会继续拒绝 WebGPU，直到发布审查把两份报告和对应 artifact set 的不可变哈希写入 production lock。

### 1.1 `0.3.0` 真实设备结论

| 平台 | 记录设备与 backend | Gate | FP32 质量 | P50 性能 |
| --- | --- | ---: | --- | ---: |
| Linux x64 glibc | NVIDIA RTX 5060 Ti / Dawn Vulkan | 164/164 | 14/14 与 CPU FP32 字节级一致 | 聚合 **5.698×**；单 fixture 3.474×–9.299× |
| Windows x64 | AMD Radeon 780M / Dawn D3D12 | 164/164 | 14/14 与 CPU FP32 字节级一致 | 聚合 **2.436×**；单 fixture 1.277×–2.982× |

聚合值为 14 个 fixture 的 CPU P50 总和除以 WebGPU P50 总和：Linux 为 `5,475.623 / 961.042 ms`，Windows 为 `6,500.853 / 2,669.160 ms`。两份报告还通过 Auto、native C++、placement profile、cold start、内存、lifecycle 与 strict rejection Gate。以上性能数字只属于表中的设备/驱动组合；其他设备不继承这些倍数。

`0.3.0` WebGPU 公共执行 profile 固定为 FP32。`Precision::fp16`/`"fp16"` 继续服务 Apple/Core ML，但 WebGPU FP16 不进入兼容范围或性能宣传。当前模型允许的 CPU partition 仅为 `Concat`、`Gather`、`Slice`；这不是“全程 GPU”声明。

Linux Native WebGPU 要求宿主提供可访问的 `/dev/dri/renderD*` DRM render node、Vulkan loader 与厂商驱动。明显不存在 render node 时，runtime 在加载 Dawn 前返回 typed `adapter_unavailable`，使 D112 Auto 可安全转入 CPU；其他未知 Vulkan/Dawn/ORT 初始化错误仍保持 fatal。

> 用户仍只安装 `@arcships/light-ocr`。稳定 release set 必须自带它声明支持的 runtime、EP、模型派生物、许可、SBOM 和 compatibility manifest；不得要求用户安装 CUDA toolkit、cuDNN、OpenVINO SDK、ROCm SDK 或编译工具链。正常 GPU/NPU driver 与发行版图形 loader 是唯一允许的系统前置条件。

## 2. 目标与非目标

### 2.1 目标

1. **保持一套 OCR 语义。** Provider 只替换 detector/recognizer inference，不复制 preprocess、DB postprocess、crop/sort、CTC decode、资源限制或结果契约。
2. **建立 Linux 跨厂商 GPU 技术基线。** 在 Intel、AMD、NVIDIA Vulkan 设备上验证当前 PP-OCRv6 Small，而不是把“Dawn 支持 Linux”当作模型已经通过。
3. **保留厂商最优路径。** WebGPU 的 API 统一不能阻止 CUDA、OpenVINO 或 MIGraphX 在用户覆盖和实测收益更高时进入独立 Spike。
4. **保持完全离线与版本固定。** Runtime、plugin、模型与缓存规则都可从 platform runtime descriptor 与 model manifest 追溯，首次运行不下载 provider 或模型。
5. **显式证明执行位置。** Session 创建成功、设备枚举和 GPU 利用率都不能代替逐节点/子图 placement 与端到端进程 CPU 时间（CPU-s）证据。
6. **允许可解释的拒绝。** 如果动态 shape、copy、冷启动、驱动或包体抵消收益，继续发布 CPU 包并记录重新启动条件。

### 2.2 非目标

- 不把 WebGPU 称为稳定 W3C Recommendation、通用 NPU API 或所有 Tier 1 平台均可用的统一 provider。
- 不在第一阶段实现 Vulkan、D3D12、Metal 或 WGSL kernels；这些属于 Dawn/ORT 或厂商 runtime 的职责。
- 不长期维护大规模 ONNX Runtime fork，也不建立项目自有的通用 GPU kernel 集。
- 不默认允许隐藏的 CPU graph partition，不因 session 可以创建就宣称“全程 GPU”。
- 不为 WebGPU 强制同步升级所有 Tier 1 平台的 ORT，也不以删除 macOS x64 支持解决依赖升级问题。
- 不在第一阶段实现 GPU preprocess/postprocess、宿主 Vulkan context 共享、零拷贝公共 API 或运行期 CPU retry。
- 不把 CUDA、OpenVINO、ROCm/MIGraphX 等开发环境当作用户运行前提。

## 3. 当前项目与模型边界

### 3.1 Runtime 与平台

当前发布和源码状态如下：

| 状态 | provider/backend | 能力边界 |
| --- | --- | --- |
| npm `0.2.0` 已发布 | `cpu` | 四个 native platform packages 只携带 CPU runtime；这是当前用户可安装的稳定能力 |
| `0.3.0` 源码候选 | `auto | cpu | apple | webgpu` | `apple` 保持 macOS Direct Core ML；Linux/Windows WebGPU FP32 产品实现、qualification package、CI 与双平台真机 Gate 已完成，production release 只待绑定报告和产物哈希 |
| 后续技术候选 | `cuda`、`openvino`、`migraphx` 等 | 尚未实现；只有 WebGPU 的真机结论不足或用户加权收益证明值得时才启动独立 Gate |

- CPU/macOS release flavor 继续固定 ONNX Runtime `1.22.0`；WebGPU flavor 精确固定 ORT Core `1.24.4` + WebGPU Plugin EP `0.1.0`，不同 flavor 不在同一进程混载。
- `InferenceSession` 已把 OCR pipeline 与 runtime 隔离；Apple Direct Core ML 证明 backend 可以不经过 ORT EP。
- `SessionExecutionInfo` 已按 detector/recognizer 报告 requested/actual provider chain、adapter identity、precision、shape policy、model/runtime/provider/qualification identity；C++ 与 Node 均输出完整 D112 selection trace。
- Node loader 在 addon 加载前验证 descriptor、平台、ABI、精确 payload inventory、每个文件 bytes/SHA-256 与 symlink；C++ 注册官方 plugin 前再次校验 provider library，并通过 ORT plugin EP API 枚举 `WebGpuExecutionProvider` devices。
- `OnnxSession::run` 当前用 CPU memory 创建输入 tensor；GPU 第一阶段必然包含 host→device 输入和 device→host 输出。
- `maxConcurrentCalls` 仍为 `1`，默认 recognition batch size 为 `1`；qualification 评估交互延迟、CPU 时间与生命周期，不用无界并发制造吞吐数字。

相关实现：

- [`cmake/Dependencies.cmake`](../cmake/Dependencies.cmake)
- [`src/inference/backend.hpp`](../src/inference/backend.hpp)
- [`src/inference/onnxruntime/backend.cpp`](../src/inference/onnxruntime/backend.cpp)
- [`include/light_ocr/types.hpp`](../include/light_ocr/types.hpp)
- [`src/core/engine.cpp`](../src/core/engine.cpp)

### 3.2 当前模型图

对锁定 `ppocrv6-small-onnx-20260714.2` 模型的本地审计结果：

| 模型 | ONNX | 节点 | 输入 shape | 主要标准算子 | 自定义 domain |
| --- | --- | ---: | --- | --- | --- |
| Detector | opset 14 | 242 | 动态 batch/H/W，NCHW 三通道 | Conv、ConvTranspose、Resize、MaxPool、GlobalAveragePool、ReduceMean、Erf、HardSigmoid、Concat、elementwise | 无 |
| Recognizer | opset 11 | 481 | 动态 batch/width，固定 `3×48` | Conv、MatMul、Softmax、BatchNormalization、Shape、Slice、Reshape、Transpose、Squeeze/Unsqueeze、Erf、Pow/Sqrt、elementwise | 无 |

这说明当前主要风险不是 Paddle custom op，而是：

- WebGPU EP 是否为**具体 opset/dtype**注册了全部需要的 kernel；
- detector 动态 H/W 与 recognizer 动态 width/batch 是否触发重复 shader/pipeline 编译或 CPU partition；
- recognition 大量短 inference 是否被 dispatch 与 copy 成本主导；
- 不同 GPU/driver 的浮点顺序、精度与边界行为是否仍通过最终 OCR 质量 Gate。

模型 manifest 与归一化配置：

- [`models/generated/ppocrv6-small-onnx-20260714.2/manifest.json`](../models/generated/ppocrv6-small-onnx-20260714.2/manifest.json)
- [`models/generated/ppocrv6-small-onnx-20260714.2/normalized-config.json`](../models/generated/ppocrv6-small-onnx-20260714.2/normalized-config.json)

## 4. 兼容栈与责任边界

WebGPU 不是网络协议，而是 GPU compute/render API 标准。Native ORT WebGPU 路径的实际栈是：

```text
PP-OCRv6 ONNX
  → ONNX Runtime graph / optimizer / partitioner
  → WebGPU Execution Provider kernels
  → Dawn WebGPU implementation + Tint/WGSL
  → Vulkan loader
  → Intel / AMD / NVIDIA Linux driver
```

| 层 | 主要责任方 | `light-ocr` 必须验证或处理的内容 |
| --- | --- | --- |
| ONNX 表达与模型图 | ONNX、PaddleOCR、模型派生工具 | opset、shape、dtype、等价 graph rewrite、模型 ID/hash/provenance |
| ORT Core | ONNX Runtime | core/plugin ABI、graph optimization、partition、profiling、session 生命周期 |
| WebGPU kernels | ONNX Runtime WebGPU EP | 当前两个模型的 kernel/opset coverage、数值与性能；缺口的上游贡献策略 |
| WebGPU native 映射 | Dawn/Tint | 固定版本与随包依赖、Vulkan adapter 枚举、validation/device-lost 行为 |
| Vulkan 与设备 | 发行版 loader、GPU driver | 支持/拒绝的 driver/device family、真实设备质量和性能证据 |
| 产品契约与分发 | `light-ocr` | API、fallback、包体、SBOM、离线安装、cache、资源、错误和支持矩阵 |

因此本项目不需要自己实现 Vulkan/D3D12/Metal backend，但仍必须按发布平台和设备族做资格验证。上游“可以在 Linux 创建 WebGPU device”只证明技术入口存在，不证明当前 OCR 模型、动态 shape 和端到端目标已经通过。

### 4.1 缺口处理顺序

发现不支持节点或行为时按以下顺序处理：

1. 用 ORT profiling、严格 `cpuPartition=forbid` 和最小复现确定问题层，不把所有失败统称为“ONNX 不支持”。
2. 如果标准 ONNX 等价分解可以保持质量和资源契约，生成独立派生模型并固定 ID/hash/provenance。
3. 如果只缺少少量通用标准算子或 opset 版本，优先向 ONNX Runtime WebGPU EP 上游贡献。
4. 私有 custom op 只允许用于范围有界、可固定分发且长期维护成本低的 provider-specific 优化。
5. 如果缺口涉及大量算子、动态 shape、precision 或架构限制，转向 CUDA/OpenVINO/MIGraphX/Direct backend，或者拒绝 WebGPU 候选。

不为通过一次 Spike 而长期维护大规模 ORT fork。任何临时 fork 都必须有上游 issue/PR、固定 revision、删除条件和独立供应链记录。

## 5. Native WebGPU 候选

### 5.1 上游状态

截至 2026-07：

- WebGPU 规范处于 W3C Candidate Recommendation Draft，并非最终 Recommendation。
- Dawn 是 Chromium 使用的跨平台 WebGPU 实现，可映射 D3D12、Metal、Vulkan 和 OpenGL；Native ORT 在 Linux 使用 Vulkan。
- ONNX Runtime WebGPU Plugin EP 的初始独立插件版本为 `v0.1.0`，要求兼容的 ORT Core `1.24.4+`；当前产品 contract 已选择 ORT `1.24.4` + official plugin `0.1.0`。
- 初始官方 plugin binaries 覆盖 Windows x64/arm64、Linux x64、macOS arm64；当前只接收 Linux x64 glibc/Vulkan 与 Windows x64/D3D12。历史 ORT `1.23.0` monolithic PoC 仅保留为技术背景，不进入任何产品 hash、ABI 或分发证据。
- Plugin 宣称覆盖主流 vision/transformer 所需的大部分标准 ONNX operators，但具体模型、opset、shape 和性能仍需应用资格验证。
- WebGPU graph capture 只适用于静态 shape 且所有 kernels 都在 WebGPU 上执行的模型；当前动态模型第一阶段不得依赖该优化。

官方依据：

- [W3C WebGPU publication history](https://www.w3.org/standards/history/webgpu/)
- [Dawn](https://dawn.googlesource.com/dawn/+/refs/heads/main/README.md)
- [ONNX Runtime WebGPU Execution Provider](https://onnxruntime.ai/docs/execution-providers/WebGPU-ExecutionProvider.html)
- [ONNX Runtime WebGPU Plugin EP v0.1.0](https://github.com/microsoft/onnxruntime/releases/tag/plugin-ep-webgpu%2Fv0.1.0)
- [ONNX Runtime plugin EP packaging guidance](https://onnxruntime.ai/docs/execution-providers/plugin-ep-libraries/packaging.html)

### 5.2 历史 PoC 与当前 qualification

2026-07-16 的本地 PoC 使用 Ubuntu 24.04 x64、NVIDIA RTX 5060 Ti、driver `590.48.01` 和 ORT `1.23.0` monolithic Dawn/Vulkan build。对确定性 FP32 tensor，detector 223 个节点全部在 WebGPU；recognizer 有 277 个 WebGPU 节点和 3 个 CPU 节点（`Slice.2`、`Concat.2`、`Gather`），因此 strict CPU partition 禁止模式下 recognizer 失败。inference-only P50 相对同机单线程 CPU 分别约为 detector 8.83×、recognizer 6.08×，数值在 `atol=1e-4, rtol=1e-3` 下 100% allclose。该证据不包含 light-ocr preprocess/postprocess、真实 OCR 质量、跨 vendor、sterile package 或 npm release，不是 PG 结论。

当前产品 qualification 已固定以下策略：

- Linux x64 glibc/Vulkan 与 Windows x64/D3D12 使用同一 ORT/plugin contract；Apple 路径不变。
- CPU、显式 WebGPU 与 D112 Auto 使用不可变的 FP32 原模型。锁定的 WebGPU FP16 派生工件只保留为可复现实验/构建输入，不属于 `0.3.0` 公共执行 profile。
- FP32 allow 必须验证实际 WebGPU placement，并把 CPU 算子限制为 `Concat/Gather/Slice`；strict 验证稳定 fail-closed，而不再要求当前图不可能达到的全 WebGPU recognition。Node Auto 和直接 C++ Auto 单独要求实际选择 WebGPU，不能把 CPU fallback 当作 PG 成功。
- `sessionFallback=error` 是唯一有效迁移值；只有 plugin 成功注册但没有兼容 adapter 的显式设备枚举结果映射 `adapter_unavailable`，未知 ORT/Dawn 失败为 fatal，不解析异常文本猜测 driver/OOM。
- 固定 `preferredLayout=NHWC`、basic validation、high-performance power preference、graph capture off；任何变动都需要重新跑完整报告。
- graph capture 关闭；只有引入静态 bucket 且全 graph placement 后才独立研究。
- preprocess、DB postprocess、crop、CTC decode 保持 CPU；I/O Binding 留到 profiler 证明 copy 是主要瓶颈之后。
- 禁止运行期 inference failure 自动重试 CPU。
- 默认一键套件覆盖锁定 14-fixture corpus、CPU FP32、WebGPU FP32 allow、strict fail-closed、Auto 与 ORT node placement；每个正常 case 固定 3 次独立 engine cold start、每次 2 次 warmup + 10 次测量（合计 30 次），另做 20 次 engine lifecycle，并检查字节级质量对齐、CPU 算子白名单、冷启动、RSS、256 MiB 解包 native payload 与性能门槛；报告带 sidecar SHA-256，报告内记录 profile hashes，原始 cases、profiles 和日志一并保留。

### 5.3 风险等级

| 风险 | 当前等级 | Spike 需要的证据 |
| --- | --- | --- |
| ORT Core/plugin ABI 与升级 | 中高 | 精确兼容矩阵、加载失败语义、平台 runtime 策略、升级复测成本 |
| 算子与 opset coverage | 中 | 两模型严格 session 创建、逐节点 placement、所有 runtime shape |
| 动态 shape 与 pipeline cache | 中高 | 冷/热 shape 序列、cache 数量与大小、无界增长检查 |
| 端到端性能 | 高 | simple/dense/tiled 的 P50/P95、CPU-s、copy 和 stage timing |
| 驱动与设备差异 | 中高 | Intel/AMD/NVIDIA 真机与最低 driver 记录；错误/结果一致性 |
| 数值与 OCR 质量 | 中 | 完整 corpus；检测、文本、置信度和临界阈值漂移 |
| 包体与供应链 | 中 | plugin/Dawn/loader 依赖、压缩/解包增量、license、SBOM、CVE owner |
| “全平台 fallback”承诺 | 不接受 | CPU 保持 Auto 稳定最终候选；WebGPU 只声明通过 Gate 的平台/设备 |

功能 PoC 的风险为中等；达到 Roadmap PG 并进入默认 platform package 的风险为中高。风险主要来自初始 plugin、动态 shape、驱动和端到端收益，不是需要本项目编写三套 native GPU backend。

## 6. 厂商 GPU/NPU 路线

WebGPU 成功不自动淘汰厂商 EP；失败也不代表 Linux 无法加速。每条路径按 HC 用户覆盖与独立 PG 排序：

| 硬件 | 首选候选 | 第一轮模型/精度 | 主要工作与限制 |
| --- | --- | --- | --- |
| NVIDIA GPU | ORT CUDA EP | 当前 FP32；记录 TF32 行为 | CUDA/cuDNN/driver 矩阵、runtime 体积、stream、copy、质量；收益通过后再做 FP16 |
| NVIDIA GPU 高吞吐 | ORT TensorRT EP | FP16 派生物 | detector min/opt/max profile、recognition width/batch profile、engine/context cache、首次编译；必须同时处理未支持节点 |
| Intel iGPU/dGPU | OpenVINO GPU | FP16 或 accuracy profile | graph coverage、driver、model cache、dynamic shape、CPU partition、包体 |
| Intel Core Ultra NPU | OpenVINO NPU | FP16；INT8/QDQ 后续 | NPU driver、固定/bounded shape、recognition buckets、detector 路由、compiled cache；不能隐藏 CPU fallback |
| AMD GPU | MIGraphX | provider 资格审查后选择 FP32/FP16 | ROCm/MIGraphX 兼容矩阵、编译/cache、算子与动态 shape；旧 ORT ROCm EP 不作为新路线 |
| AMD Ryzen AI NPU | Vitis AI，若 Linux 目标与分发可行 | INT8/BF16 provider-specific | 厂商模型派生、校准、编译/context、硬件与 OS 范围；与 MIGraphX 分开决策 |
| Qualcomm/Rockchip/华为等 NPU | QNN/RKNPU/CANN 等 | 厂商专用 QDQ/context/IR | 通常依赖 Linux arm64 或特定设备；当前无 Tier 1 交集，等待 D110 与真实需求 |

官方参考：

- [ONNX Runtime CUDA EP](https://onnxruntime.ai/docs/execution-providers/CUDA-ExecutionProvider.html)
- [ONNX Runtime TensorRT EP](https://onnxruntime.ai/docs/execution-providers/TensorRT-ExecutionProvider.html)
- [ONNX Runtime OpenVINO EP](https://onnxruntime.ai/docs/execution-providers/OpenVINO-ExecutionProvider.html)
- [OpenVINO NPU device](https://docs.openvino.ai/2026/openvino-workflow/running-inference/inference-devices-and-modes/npu-device.html)
- [ONNX Runtime MIGraphX EP](https://onnxruntime.ai/docs/execution-providers/MIGraphX-ExecutionProvider.html)
- [ONNX Runtime QNN EP](https://onnxruntime.ai/docs/execution-providers/QNN-ExecutionProvider.html)

## 7. Runtime 与 Backend 架构

### 7.1 Provider registry

Linux provider 不继续堆叠在 `Engine::create` 的平台条件分支中。目标内部结构是：

```text
ExecutionOptions + platform runtime descriptor
  → provider registry / compatibility check
  → exact backend factory
      ├── ORT CPU
      ├── ORT WebGPU plugin
      ├── ORT CUDA/OpenVINO/MIGraphX build or plugin
      └── direct backend, only when independently accepted
  → atomic BackendSessionPair
      ├── detector InferenceSession
      └── recognizer InferenceSession
  → immutable selection trace + per-session execution info
```

Registry 必须：

- 只加载 platform runtime descriptor 声明且当前 package 实际携带的 provider；该 descriptor 由 release staging 从实际 payload 生成，模型 manifest 不承载 runtime capability；
- 从 package 私有、哈希已验证的绝对路径注册 plugin，不扫描 cwd、`PATH`、`LD_LIBRARY_PATH`、Python site-packages 或厂商 SDK；
- 在 native load 前完成无副作用的 descriptor/设备兼容检查；
- 每个进程/worker 首版只加载一种兼容 ORT/backend runtime；
- 遵循 D112：Auto 在创建期间可对可跳过原因销毁候选并继续；成功后冻结 provider，`Run` 失败时不动态注入另一个 runtime；
- detector/recognizer 可以在同一 backend 内使用不同 device/profile，但必须分别报告；
- 保留当前 `maxConcurrentCalls=1` 的生命周期语义，throughput 作为独立 profile 研究。

### 7.2 ORT 版本升级

产品 contract 已选择官方 plugin 拓扑，不复用历史 monolithic build：`Microsoft.ML.OnnxRuntime@1.24.4` 提供 Core/headers，`Microsoft.ML.OnnxRuntime.EP.WebGpu@0.1.0` 提供 plugin 与 Dawn 附属库；NuGet URL、catalog、bytes、SHA-512、ZIP members、staged paths、headers、licenses 和 platform identity 全部锁在 [`tools/webgpu/runtime-lock.json`](../tools/webgpu/runtime-lock.json)。assembler 与 CMake 会分别复核完整 inventory/hash，`.cache` PoC 二进制没有进入生产输入的路径。

当前采用 platform runtime flavor：Linux/Windows WebGPU package 使用 ORT 1.24.4 plugin ABI，CPU/macOS flavor 保留 ORT 1.22.0 或 Direct Core ML；facade 一次只加载当前平台 package，进程内不混载两个 ORT ABI。未来统一版本仍需单独评估，不能借本次实现删除 macOS x64 Tier 1。

真实设备 Gate 后的 release 决策仍可在以下结果中选择：

1. 接受当前各自精确锁定的 platform runtime flavor，并把 WebGPU artifact/report hashes 写入 production lock；
2. 缩减 WebGPU 的平台、driver、设备或 partition 范围，保持 qualification-only；
3. 如果质量、严格 placement、性能、内存或维护成本失败，拒绝 WebGPU 发布并保持 CPU/Direct Core ML 路线。

不得把删除 macOS x64 Tier 1 支持当作 Linux WebGPU 的隐含实现步骤；平台变更只能通过 D110 独立决定。

### 7.3 Shape、precision 与 cache

- **WebGPU：** 第一轮保持动态 shape、FP32 和普通执行；记录每个新 detector H/W、recognizer width/batch 的首次 pipeline 成本。只有 cache 有界后才尝试静态 bucket/graph capture。
- **CUDA：** 当前动态模型作为最低改造基线；TF32/FP16 分开做质量与可复现性 Gate。
- **TensorRT：** detector 使用预注册 min/opt/max 或有限 profiles；recognizer 复用受控 width buckets 和 batch ceiling；engine/context cache key 必须包含 model/provider/device/driver/shape/precision。
- **OpenVINO GPU/NPU：** GPU 可先验证动态/bounded shape；NPU 优先复用 recognition width buckets，detector 若无法高质量、完整放置，可由同一 OpenVINO backend 的 GPU 或明确 CPU session 执行并如实报告。
- **MIGraphX/Vitis AI/QNN：** 每种精度和 context 都是独立模型派生物，不复用 Apple、OpenVINO 或另一厂商的量化质量结论。

所有 cache 必须有版本化 key、大小 ceiling、损坏恢复、并发写入和清理策略；运行时不得下载编译器。

## 8. 分发与安装

目标安装仍为：

```text
@arcships/light-ocr
  └── @arcships/light-ocr-linux-x64-gnu
      ├── light_ocr_node.node
      ├── pinned runtime + CPU final candidate
      ├── accepted WebGPU/plugin payload, if PG passes
      ├── accepted vendor payloads, only after independent PG/package review
      ├── platform runtime descriptor + provider compatibility manifest
      └── licenses / SBOM / provenance
```

npm 不能按 GPU vendor 过滤 optional dependency。每增加一个默认 Linux x64 payload，所有该平台安装都会承担其下载和磁盘成本。因此：

- WebGPU 只有在用户加权覆盖和 package-size Gate 通过后才进入默认 Linux platform package；
- CUDA/OpenVINO/MIGraphX 在通过技术 Spike 但未通过默认包成本审查时，只保留 qualification artifact，不把 runtime 安装责任交给用户；
- 内部同版本 shard 可以改善制品组织，但如果 facade/platform package 会自动取得它，仍必须按用户实际总下载量计入 Gate；
- 不使用 install/postinstall、首次运行下载、系统 SDK 探测或源码编译 fallback；
- 正常 GPU driver、Vulkan loader 或 NPU driver 可以作为系统前置条件，但最低版本和错误必须写入 manifest/diagnostics。

## 9. 公共 API 与 fallback

WebGPU Preview 只有通过 PG 并实际随 release set 交付后才加入公开 union：

```ts
const engine = await createEngine({
  execution: {
    provider: 'webgpu',
    sessionFallback: 'error',
    cpuPartition: 'forbid',
    precision: 'fp32',
  },
});
```

规则：

- Linux 平台默认方向是 D112 `auto`；已发布策略只包含实际通过 Gate 并随包交付的候选。WebGPU 尚未交付的版本不得假装尝试它。
- 目标完整策略为 `webgpu → cpu`；显式 `webgpu` 或 `cpu` 只创建指定 backend，失败直接传播。
- `sessionFallback` 仅作为迁移字段保留；Auto 和显式 provider 都只接受 `error`，任何 `sessionFallback=cpu` 都返回 `invalid_argument`。
- 首版 Auto 使用 provider-neutral 默认值；`cpuPartition=forbid`、显式 `precision=fp32` 或 `deviceId` 的资格测试必须使用显式 WebGPU。
- `cpuPartition` 控制同一 ORT session 内的节点/子图 CPU placement，不属于跨 backend fallback。
- `Run` 开始后的 device lost、driver reset、OOM 或 inference failure 返回错误，不自动重跑 CPU。
- 创建成功的 `EngineInfo` 报告 D112 Auto policy/attempt trace 与 detector/recognizer 的实际 provider、device、precision 和 partition；创建失败的同构 trace 在结构化 creation error 中，不能只显示最终 CPU 或依赖错误消息解析。

## 10. Benchmark 与 Provider Gate

Linux provider 继承 Roadmap PG，使用相同模型、pre/postprocess、资源限制和结果 schema。正式候选至少包括：

| Workload | 目的 |
| --- | --- |
| `generated-hello-123` | 小图、启动和 dispatch/copy 是否抵消收益 |
| `paddleocr-xfund-form` | 高文本密度、recognition 调用和 CPU 释放 |
| `tiled-v1` locked corpus | 多 detector pass、global merge、recognition 与内存 ceiling |
| recognition width/batch sweep | 动态 shape、pipeline/cache 数量、短 inference 与吞吐 |

这里的 CPU-s 指 OCR 进程在一次 workload 中消耗的用户态与内核态 CPU 时间总和；CPU 对照必须使用同机、同模型、同 workload、同资源限制和同质量 profile。

一键 runner 的当前报告包含：

- package/runtime/plugin load、device enumeration、session compile、first result；
- 14 个锁定 fixture 的 2 次 warm-up 与 10 次 warm measurement，以及 canary 20 次独立 engine create/close；
- total 与各 stage P50/P95、throughput、OCR process CPU-s；
- host RSS、retained growth、artifact/package bytes 与总安装 inventory；device memory 若上游未提供跨平台 API 则明确留作人工设备证据，不伪造数值；
- provider/device/driver/runtime/plugin、shape、batch、precision、电源模式；
- ORT/provider profiling 的节点/子图 placement 与 CPU partition；
- schema/runtime contract、14-fixture CPU-vs-WebGPU text/confidence/box parity、determinism 与稳定错误；
- 冷启动、20 次 close/recreate lifecycle、raw case、profile 和命令日志。

通过标准：

- 至少两个锁定 fixture 达到 `CPU P50 / provider P50 ≥ 1.5`，14-fixture P50 总和 speedup ≥1.1，且任一 fixture 的 WebGPU P95 不超过 CPU 3×；
- contract 100% 通过，质量在查看性能结果前预注册的容差内；
- 无绕过 D112 的整 session fallback；CPU graph partition 必须在报告中量化并计入端到端结果；
- OCR process CPU-s/cores 作为报告和兼容范围审查指标；当前不会用一个未被 runner 强制的 80% 数字冒充自动 Gate；
- canary engine initialization + first result ≤30 s，进程 resident maximum ≤2 GiB，20 次 lifecycle retained growth 绝对值 ≤128 MiB；
- 干净 Linux 环境仅安装正常 driver/loader，从本地 npm tarball 禁网安装并运行。

设备证据分级：

- 单一设备可以完成技术 PoC，但不能产生跨厂商声明；
- `webgpu` Linux Preview 至少需要两个 GPU vendor 的真实设备通过同一 Gate；
- “Linux x64 Intel/AMD/NVIDIA 跨厂商 baseline”声明要求三家各至少一台预注册设备通过；
- 未验证设备可以开放实验兼容，但必须报告 `deviceValidated=false`，不继承其他 vendor/family 的性能数字；
- 某 vendor 失败时允许缩减 compatibility manifest，不因另外两个 vendor 成功而宣称完整覆盖。

### 10.1 接受、缩减与拒绝矩阵

| 结论 | 必要条件 | 产品动作 |
| --- | --- | --- |
| 接受为 Linux WebGPU Preview | 所有 contract、质量、安全、供应链和资源 Gate 通过；至少两个预注册 GPU vendor 在至少两个目标 workload 上通过性能 Gate；支持的 shape、driver 和 CPU partition 范围已锁定 | 仅把通过范围写入 compatibility manifest；通过 package review 后才加入 `webgpu` 公共 union 与 release set |
| 缩减 | 非性能否决项全部通过，但收益只在预注册的 vendor、device family、shape 或 workload 子集成立；若允许 CPU partition，最低 placement coverage 必须由 D111 预注册并仍通过端到端 PG | 明确收窄 manifest 和文案，不宣称“全程 GPU”或 Linux 全覆盖；只有单一 vendor 的结果默认保持 qualification-only，除非 D111 独立接受 vendor-scoped Preview |
| 拒绝本轮 WebGPU | contract/质量/安全/供应链任一否决项失败；CPU partition 超出锁定的三类算子；strict 未能稳定 fail-closed；或包体/driver 维护超出预注册边界 | 不加入公共 API 或默认包，CPU 与其他厂商路径继续；记录失败层、证据和可测量的重启条件 |

重启条件只能是可验证的外部或产品变化，例如上游补齐具体 kernel/shape、发布兼容 plugin、目标用户设备结构变化，或新的 workload 使收益门槛可能成立；不能仅以“再试一次”重开。

## 11. 分阶段落地

Phase A/B 是已经接受的 Linux WebGPU 主线执行顺序。HC 不再以厂商路径替换该主线；它决定设备资格范围和 Phase C 专用后端是否满足启动 Gate。

### Phase A — ORT/WebGPU 技术 Spike

- 已在一个 NVIDIA Vulkan GPU 上完成 monolithic PoC、模型加载、FP32 数值对照和 inference-only benchmark；detector strict 通过，recognizer 因 3 个 CPU 节点 strict 失败。
- 已选择 official ORT Core `1.24.4` + WebGPU plugin `0.1.0`，提交跨平台可复现 artifact lock、在线/离线 assembler 与精确 SDK verifier。
- 已完成 C++/Node plugin registration、D112 Auto、FP32 WebGPU 路由、strict fail-closed、descriptor、npm self-contained payload、license/SBOM 与 qualification-only release gate。
- 已完成内部 FP16 工件的可复现派生与确定性运行验证、Linux/Windows hardware-independent CI 和完整真机 runner；公共 WebGPU profile 已根据双平台结果收敛为 FP32。

退出条件：实现、硬件无关验证与双平台真实设备证据均已满足；当前只待审阅并绑定报告/artifact hashes 后修改 production-qualified fields。

### Phase B — WebGPU Linux Preview Gate

- 先在用户提供的 Linux x64 与 Windows x64 真实 GPU 上运行同一 one-command suite，回收 artifact-bound reports；再决定是否需要扩展到 Linux 第二/第三 vendor。
- provider registry、固定路径 plugin load、D112 `webgpu → cpu`、attempt trace 和 close 语义已经实现；本阶段只根据真实证据修复或收窄，不重新定义成功。
- 审查 FP32 placement、strict rejection、完整质量、性能、内存、生命周期和离线 package Gate。
- FP16 不进入 `0.3.0` WebGPU 产品 profile；未来重启 FP16 必须重新通过质量与双平台性能 Gate，不为追数字跳过质量 Gate。

退出条件：`webgpu` 被接受为明确平台/设备范围的 Preview、缩减为 qualification-only/vendor-scoped 候选，或记录拒绝与可测量的重启条件。

### Phase C — 专用厂商 GPU/NPU

只有 WebGPU 在目标设备/workload 上未通过覆盖、质量、性能或包体 Gate，或专用路径的用户加权收益足够覆盖维护成本时才启动；顺序由 HC 决定：

- NVIDIA 用户/服务端 workload 权重高：CUDA FP32/TF32 → FP16 → TensorRT profile/cache；
- Intel 跨 Windows/Linux 用户权重高：OpenVINO GPU FP16 → Core Ultra NPU FP16 → 独立 INT8/QDQ；
- AMD GPU 用户权重高且 WebGPU 收益不足：MIGraphX；Ryzen AI NPU 另做 Vitis AI/OS 分发决策；
- Linux arm64/边缘用户达到 D110 门槛后，再评估 QNN/RKNPU/CANN 等专用 NPU。

每个 provider 独立通过 PG 和 package review，不因 WebGPU 或另一厂商成功而自动发布。

### Phase D — I/O Binding 与吞吐

- profiler 证明 copy 已成为主要瓶颈后，再加入 ORT I/O Binding、固定 shape buffer 和 device tensor 复用；
- 先使用有界 recognition batch 4/8，再研究多 engine/stream；
- GPU preprocess/postprocess 和宿主 Vulkan/CUDA context 只在独立 ABI、同步和生命周期设计通过后进入实验；
- 新优化必须重新运行 provider × model × workload 的质量和资源 Gate。

## 12. 仍需 Provider Gate 决定的问题

跨 backend Auto 候选序、创建失败分类、显式 provider 和运行期冻结已由 D112 决定；本节只保留 Linux provider-local 资格问题。

1. 用户的 Linux 与 Windows GPU/driver 是否都通过 FP32 placement/质量、strict rejection、冷启动、性能、RSS 与 lifecycle Gate？当前记录设备已通过；新增设备需独立回答。
2. 含锁定三类 CPU graph partition 的 FP32 Preview 是否可接受，最低 placement coverage 如何写入兼容范围？
3. 当前设备报告能支持哪一层声明：单设备实验、vendor-scoped、双 vendor Preview，还是继续 qualification-only？
4. Preview 最低 Vulkan/D3D12 driver、GPU family 与 feature/limit 如何写入 compatibility range？
5. WebGPU payload 的实际压缩/解包增量与用户加权收益是否值得进入所有 Linux/Windows x64 默认安装？
6. 当前 bounded/960 与动态 recognition width 范围是否全部接受，还是需要收窄 shape 范围？
7. CUDA/OpenVINO/MIGraphX 只能通过不同 ORT build 时，是否使用内部 worker 隔离，还是保持 qualification-only？
8. Linux arm64 何时进入 Tier 1，从而允许 WebGPU arm64 或专用 NPU 成为产品路径？

## 13. 结论边界

本方案接受 **FP32 WebGPU 作为 Linux x64 通用加速方向**。当前 NVIDIA/Linux 与 AMD/Windows 记录设备的 PG 已通过，但它们不自动证明所有设备/驱动兼容，也不等于 npm 制品已经发布；ONNX Runtime 不会替项目承担产品兼容工作。

`0.3.0` 产品结论来自已提交的真实设备完整 OCR Gate：Linux RTX 5060 Ti/Vulkan 与 Windows Radeon 780M/D3D12 支持发布为明确平台边界的 WebGPU Preview，公开精度固定 FP32，Auto 保留 CPU 最终候选。其他 vendor/device/driver 通过开放兼容路径试用但不继承已记录性能数字；若后续设备不成立，应收窄兼容范围，而不是隐藏回退。CUDA/OpenVINO/MIGraphX 仍按专用后端启动 Gate 作为独立候选。任何结论都不要求本项目实现 D3D12/Vulkan/Metal 三套 backend，也不允许用大规模 ORT fork 掩盖上游能力边界。
