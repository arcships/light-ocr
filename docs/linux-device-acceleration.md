# Linux Device 加速技术方案

状态：Draft；用于 Perf-2/D111 方案讨论，不代表已经批准实现或发布

更新时间：2026-07-16

范围：当前交付目标是 Linux x64 glibc；Linux arm64、移动端与边缘 NPU 属于后续平台决策

关联 Roadmap：[Perf-0–Perf-4](roadmap.md#7-perf-0perf-4--性能与宿主加速线)

## 1. 结论

Linux 加速可复用当前 ONNX 模型和 backend-neutral `InferenceSession` 边界，不需要重写 OCR pipeline。推荐把候选分成两层：

- **Native ONNX Runtime WebGPU EP 是跨厂商 GPU baseline 候选。** 它通过 Dawn 在 Linux 上使用 Vulkan，第一轮直接验证当前 FP32 ONNX detector/recognizer，不先制作 FP16 模型，也不先建设 GPU preprocess。
- **厂商路径负责最优性能和 NPU。** NVIDIA 优先比较 CUDA，收益成立后再研究 TensorRT；Intel 使用 OpenVINO GPU/NPU；AMD GPU 使用 MIGraphX。NPU 没有 Linux 全平台统一 API，必须按厂商、架构、模型精度和编译产物分别验证。
- **CPU 是默认稳定 backend，也是唯一可显式配置的整 session fallback 目标。** `sessionFallback` 默认仍为 `error`，运行期 inference failure 不自动重试 CPU。WebGPU 是 GPU provider 候选，不是 NPU API，也不是“任何机器都更快”的全平台兜底。
- **兼容层由上游实现，产品兼容由本项目负责。** ONNX Runtime WebGPU EP 提供算子 kernels，Dawn/Tint 映射 Vulkan/D3D12/Metal；`light-ocr` 仍负责模型覆盖、驱动矩阵、包体、质量、性能、资源和显式 fallback 契约。

如果 HC/D111 把 Linux WebGPU 选为首批候选，首个动作是 Linux x64 严格 WebGPU Spike，而不是同时铺开 Windows/macOS：固定兼容 ORT Core 与 WebGPU plugin，禁止 CPU graph partition，生成 detector/recognizer 的算子、shape、placement 和端到端报告。如果 HC 选择其他路径，则对应厂商 Spike 前移。任何 Spike 都可以得到接受、缩减或拒绝结论；失败不阻塞其他 provider 或 CPU 主线。

> 用户仍只安装 `@arcships/light-ocr`。稳定 release set 必须自带它声明支持的 runtime、EP、模型派生物、许可、SBOM 和 compatibility manifest；不得要求用户安装 CUDA toolkit、cuDNN、OpenVINO SDK、ROCm SDK 或编译工具链。正常 GPU/NPU driver 与发行版图形 loader 是唯一允许的系统前置条件。

## 2. 目标与非目标

### 2.1 目标

1. **保持一套 OCR 语义。** Provider 只替换 detector/recognizer inference，不复制 preprocess、DB postprocess、crop/sort、CTC decode、资源限制或结果契约。
2. **建立 Linux 跨厂商 GPU 技术基线。** 在 Intel、AMD、NVIDIA Vulkan 设备上验证当前 PP-OCRv6 Small，而不是把“Dawn 支持 Linux”当作模型已经通过。
3. **保留厂商最优路径。** WebGPU 的 API 统一不能阻止 CUDA、OpenVINO 或 MIGraphX 在用户覆盖和实测收益更高时进入独立 Spike。
4. **保持完全离线与版本固定。** Runtime、plugin、模型与缓存规则都可从 release manifest 追溯，首次运行不下载 provider 或模型。
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
| `0.2.1` 主干源码候选 | `cpu | apple` | `apple` 是 macOS Direct Core ML backend，已通过 M4 Max 本地 Gate，但尚未发布；Linux `OnnxSession` 仍拒绝非 CPU provider |
| Perf-2 规划候选 | `webgpu`、`cuda`、`openvino`、`migraphx` 等 | 尚未实现，也未进入当前公开 npm union；只有通过 HC、PG、D111 和 release-set 审查后才可能加入 |

- 项目固定 ONNX Runtime `1.22.0` CPU package；Linux 只选择 `linux-x64` runtime。
- `InferenceSession` 已把 OCR pipeline 与 runtime 隔离；Apple Direct Core ML 证明 backend 可以不经过 ORT EP。
- `SessionExecutionInfo` 已能按 detector/recognizer 报告 requested/actual provider、device、precision、shape policy、model/runtime/cache identity 和 fallback。
- `OnnxSession::run` 当前用 CPU memory 创建输入 tensor；GPU 第一阶段必然包含 host→device 输入和 device→host 输出。
- `maxConcurrentCalls` 仍为 `1`，默认 recognition batch size 为 `1`；第一阶段先评估交互延迟和 CPU 释放，不用无界并发制造吞吐数字。

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
- ONNX Runtime WebGPU Plugin EP 的初始版本为 `v0.1.0`，要求兼容的 ORT Core `1.24.4+`。
- 初始官方 native binaries 覆盖 Windows x64/arm64、Linux x64、macOS arm64；没有 macOS x64、Linux arm64 和移动端。
- Plugin 宣称覆盖主流 vision/transformer 所需的大部分标准 ONNX operators，但具体模型、opset、shape 和性能仍需应用资格验证。
- WebGPU graph capture 只适用于静态 shape 且所有 kernels 都在 WebGPU 上执行的模型；当前动态模型第一阶段不得依赖该优化。

官方依据：

- [W3C WebGPU publication history](https://www.w3.org/standards/history/webgpu/)
- [Dawn](https://dawn.googlesource.com/dawn/+/refs/heads/main/README.md)
- [ONNX Runtime WebGPU Execution Provider](https://onnxruntime.ai/docs/execution-providers/WebGPU-ExecutionProvider.html)
- [ONNX Runtime WebGPU Plugin EP v0.1.0](https://github.com/microsoft/onnxruntime/releases/tag/plugin-ep-webgpu%2Fv0.1.0)
- [ONNX Runtime plugin EP packaging guidance](https://onnxruntime.ai/docs/execution-providers/plugin-ep-libraries/packaging.html)

### 5.2 第一轮配置

第一轮 WebGPU Spike 固定以下策略：

- 仅 Linux x64 glibc；不同时修改 Apple/Windows 产品路径。
- 使用当前 FP32 ONNX 原模型和 FP32 输入/输出；不先制作 FP16 派生物。
- 显式 provider，不启用 `auto`。
- `cpuPartition=forbid` 用于 coverage qualification；另行测量 `allow` 只能作为声明清楚的缩减候选。
- `sessionFallback=error` 用于正式 placement/性能报告；初始化 fallback 另做稳定错误与 CPU canary 测试。
- 初始使用 NCHW preferred layout；任何 layout change 都需要完整质量和内存验证。
- graph capture 关闭；只有引入静态 bucket 且全 graph placement 后才独立研究。
- preprocess、DB postprocess、crop、CTC decode 保持 CPU；I/O Binding 留到 profiler 证明 copy 是主要瓶颈之后。
- 禁止运行期 inference failure 自动重试 CPU。

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
| “全平台 fallback”承诺 | 不接受 | CPU 保持稳定 fallback；WebGPU 只声明通过 Gate 的平台/设备 |

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
ExecutionOptions + release provider manifest
  → provider registry / compatibility check
  → exact backend factory
      ├── ORT CPU
      ├── ORT WebGPU plugin
      ├── ORT CUDA/OpenVINO/MIGraphX build or plugin
      └── direct backend, only when independently accepted
  → detector InferenceSession
  → recognizer InferenceSession
  → immutable SessionExecutionInfo
```

Registry 必须：

- 只加载 release manifest 声明且当前 package 实际携带的 provider；
- 从 package 私有、哈希已验证的绝对路径注册 plugin，不扫描 cwd、`PATH`、`LD_LIBRARY_PATH`、Python site-packages 或厂商 SDK；
- 在 native load 前完成无副作用的 manifest/设备兼容检查；
- 每个进程/worker 首版只加载一种兼容 ORT/backend runtime；
- session 创建后冻结 provider，不在 `Run` 失败时动态注入另一个 runtime；
- detector/recognizer 可以在同一 backend 内使用不同 device/profile，但必须分别报告；
- 保留当前 `maxConcurrentCalls=1` 的生命周期语义，throughput 作为独立 profile 研究。

### 7.2 ORT 版本升级

当前 ORT `1.22.0` 不能直接加载要求 ORT `1.24.4+` 的初始 WebGPU plugin。Spike 使用**平台限定升级**：Linux x64 生成独立、精确锁定的新 ORT Core + WebGPU plugin 实验制品，不让探索性升级改变 macOS/Windows 已发布 runtime。

进入 Perf-3 前，D111 Linux 子决策必须在以下方案中选定：

1. 接受 platform package 使用不同但各自精确锁定的 ORT 版本；
2. 为需要保留但上游不再提供二进制的平台自建并资格验证统一 ORT；
3. 如果版本、ABI、包体或维护成本过高，拒绝 WebGPU 发布并保持现有 CPU/Direct Core ML 路线。

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
      ├── pinned ORT Core + CPU fallback
      ├── accepted WebGPU/plugin payload, if PG passes
      ├── accepted vendor payloads, only after independent PG/package review
      ├── provider compatibility manifest
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
    sessionFallback: 'cpu',
    cpuPartition: 'forbid',
    precision: 'fp32',
  },
});
```

规则：

- `cpu` 保持默认；`webgpu` 不作为默认或隐式同义词 `gpu`。
- `sessionFallback` 只覆盖 provider/device 不可用或 session 初始化失败后的整 session CPU fallback，并记录稳定原因。
- `cpuPartition` 控制同一 ORT session 内的节点/子图 CPU placement，与 session fallback 分离。
- `Run` 开始后的 device lost、driver reset 或 inference failure 返回错误，不在首版自动重跑 CPU。
- `auto` 只能从 package manifest 中已经通过 Gate 且实际携带的 provider 按版本化策略选择；WebGPU 存在不自动使 `auto` 可用。
- `EngineInfo.execution.sessions` 是实际执行权威来源；请求 `webgpu` 但实际运行 CPU 必须明确显示 fallback/partition，不能只返回请求值。

## 10. Benchmark 与 Provider Gate

Linux provider 继承 Roadmap PG，使用相同模型、pre/postprocess、资源限制和结果 schema。正式候选至少包括：

| Workload | 目的 |
| --- | --- |
| `generated-hello-123` | 小图、启动和 dispatch/copy 是否抵消收益 |
| `paddleocr-xfund-form` | 高文本密度、recognition 调用和 CPU 释放 |
| `tiled-v1` locked corpus | 多 detector pass、global merge、recognition 与内存 ceiling |
| recognition width/batch sweep | 动态 shape、pipeline/cache 数量、短 inference 与吞吐 |

这里的 CPU-s 指 OCR 进程在一次 workload 中消耗的用户态与内核态 CPU 时间总和；CPU 对照必须使用同机、同模型、同 workload、同资源限制和同质量 profile。

每次报告必须包含：

- package/runtime/plugin load、device enumeration、session compile、first result；
- 3 次独立 cold start、至少 5 次 warm-up 与 30 次 warm measurement；
- total 与各 stage P50/P95、throughput、OCR process CPU-s；
- host RSS、可获取的 device memory、cache 数量/大小和总安装增量；
- provider/device/driver/runtime/plugin、shape、batch、precision、电源模式；
- ORT/provider profiling 的节点/子图 placement 与 CPU partition；
- 现有 contract、14-fixture parity/quality、tiled quality 和稳定错误；
- cache miss/hit、损坏恢复、并发进程、device lost、close 与 100-page lifecycle。

通过标准：

- 至少两个预注册目标 workload 达到 `CPU P50 / provider P50 ≥ 1.5`，或目标 throughput ≥2× CPU；
- contract 100% 通过，质量在查看性能结果前预注册的容差内；
- 无未声明的整 session fallback；CPU graph partition 必须在报告中量化并计入端到端结果；
- 交互式候选相对上述同机 CPU baseline 的 OCR process CPU-s 目标至少下降 80%，最终阈值由 D111 在正式 benchmark 前锁定；
- cold-start、RSS/device memory、cache、包增量、最低 driver 与允许的 device/driver 范围全部在正式测量前锁定并通过；
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
| 拒绝本轮 WebGPU | contract/质量/安全/供应链任一否决项失败；两个模型无法在严格模式创建且没有有界等价改写；包体/driver 维护超出预注册边界；或 FP32 基线加至多一个由 profiler 证明的优化分支后仍无目标 workload 通过性能 Gate | 不加入公共 API 或默认包，CPU 与其他厂商路径继续；记录失败层、证据和可测量的重启条件 |

重启条件只能是可验证的外部或产品变化，例如上游补齐具体 kernel/shape、发布兼容 plugin、目标用户设备结构变化，或新的 workload 使收益门槛可能成立；不能仅以“再试一次”重开。

## 11. 分阶段落地

Phase A/B 是 **HC/D111 选择 WebGPU 后**的执行顺序，不是绕过 HC 的固定优先级。如果 HC 选择 CUDA、OpenVINO 或 MIGraphX，对应 Phase C 分支成为首个 Spike，WebGPU 保持候选。

### Phase A — ORT/WebGPU 技术 Spike

- 固定 Linux x64 实验用 ORT Core、WebGPU plugin、Dawn 依赖与 hashes。
- 在一个可用 Vulkan GPU 上完成 plugin 注册、设备选择和当前模型加载。
- 对 detector/recognizer 跑严格 `cpuPartition=forbid`，输出算子/opset/shape/placement 报告。
- 跑 `generated-hello-123`、`paddleocr-xfund-form` 和动态 width sweep，记录 cold/warm、CPU-s、RSS/VRAM 和 cache。
- 生成压缩/解包包增量、依赖树、license、SBOM 与兼容失败报告。

退出条件：得到可复现的接受、缩减或拒绝技术结论；不修改默认 CPU package，不进入公开 provider union。

### Phase B — WebGPU Linux Preview Gate

- 按 HC/D111 固定至少两个、目标为三个 GPU vendor 的设备与 driver。
- 实现 provider registry、固定路径 plugin load、显式 fallback、diagnostics 和 device-lost/close 语义。
- 跑完整质量、性能、cache、内存、生命周期和禁网 package Gate。
- 如果 FP32 已通过 placement 但收益不足，先判断 copy/dispatch/shape cache 是否是主因，再决定是否进入 FP16、bucket 或 I/O Binding；不为追数字跳过质量 Gate。

退出条件：`webgpu` 被接受为明确平台/设备范围的 Preview、缩减为 qualification-only/vendor-scoped 候选，或记录拒绝与可测量的重启条件。

### Phase C — 厂商 GPU/NPU

由 HC 用户覆盖决定顺序，不按全球硬件市占率预设：

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

## 12. 仍需 D111 决定的问题

1. Linux WebGPU Spike 固定 ORT `1.24.4` 最小兼容版本，还是选择更新的受支持 patch；Core/plugin 的精确兼容规则是什么？
2. Linux 与其他 platform package 是否允许使用不同 ORT 版本；若不允许，macOS x64 的构建与资格成本由谁承担？
3. WebGPU plugin、Dawn 与附属动态库的压缩/解包增量 ceiling 是多少？
4. Preview 最低 Vulkan loader、Intel/AMD/NVIDIA driver 和 GPU feature/limit 如何写入 manifest？
5. `cpuPartition=allow` 是否进入 Preview，还是首个公开 WebGPU provider 只接受完整 graph；允许时最低 coverage 如何定义？
6. 两 vendor Preview 与三 vendor broad baseline 的目标设备分别是什么，设备证据由维护者还是社区提供？
7. 当前动态 detector shapes 是否保持开放，还是只接受 bounded/960 与 `tiled-v1` 的受控 shape 集？
8. recognition width cache 的数量、RSS/VRAM 和 shader/pipeline 编译 ceiling 是多少？
9. WebGPU 是否值得进入所有 Linux x64 默认安装，还是技术成功但因包体/用户覆盖不进入 release set？
10. CUDA/OpenVINO/MIGraphX 只能通过不同 ORT build 时，是否使用内部 worker 隔离，还是保持 qualification-only？
11. Linux arm64 何时进入 Tier 1，从而允许 WebGPU arm64 或专用 NPU 成为产品路径？

## 13. 结论边界

本方案接受的只是**验证路线**：当前标准 ONNX 模型、backend 抽象和 Native WebGPU plugin 使 Linux 跨厂商 GPU Spike 具有合理成本。它不预先接受 WebGPU 发布，也不声称 ONNX Runtime 会替项目承担所有兼容工作。

最终产品结论必须来自真实设备上的完整 OCR Gate：如果 WebGPU 在动态 shape、驱动、包体和端到端收益上成立，它成为显式跨厂商 GPU Preview；如果只在部分 vendor/workload 成立，就缩减支持范围；如果不成立，CPU 保持默认稳定 backend 和显式 fallback 目标，CUDA/OpenVINO/MIGraphX 继续作为独立候选。任何结论都不要求本项目实现 D3D12/Vulkan/Metal 三套 backend，也不允许用大规模 ORT fork 掩盖上游能力边界。
