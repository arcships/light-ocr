# Intel NPU 加速技术方案

状态：Phase B 核心已在源码实现（qualification build）；Linux x64 真机 14-fixture 验证通过；未进入 released policy，npm 包未包含 OpenVINO，不代表已经发布；目标是 Intel NPU 主机上 Auto 优先使用 NPU

更新时间：2026-09-29

范围：第一目标是 Linux x64 glibc 上的 Intel Core Ultra NPU；Windows x64 作为同一后端的第二平台；Intel iGPU/dGPU 继续走已发布的 Native WebGPU 主线，不在本方案内新建 GPU 后端

关联文档：[Linux Device 加速技术方案 §6](linux-device-acceleration.md#6-厂商-gpunpu-路线)、[Windows Device 加速技术方案](windows-device-acceleration.md)、[Apple Device 加速技术方案](apple-device-acceleration.md)、[D111](decisions.md#d111--freeze-a-provider-neutral-execution-contract-before-enabling-accelerators)、[D112](decisions.md#d112--use-platform-aware-auto-with-creation-time-ordered-fallback)

## 1. 结论

Intel NPU 需要一个**新的 Direct OpenVINO 推理后端**，但不需要新的 OCR pipeline：

- **WebGPU 无法使用 NPU。** Native WebGPU 经 Dawn 只枚举 Vulkan/D3D12/Metal 适配器；Linux 上的 Intel NPU 由 `intel_vpu` 内核驱动（`/dev/accel/accel0`）与 Level Zero 暴露，没有 Vulkan ICD。因此 NPU 只能由厂商 runtime 驱动，项目文档中既定的厂商路径是 OpenVINO。
- **Intel GPU 继续走 WebGPU。** Arc B390（Panther Lake Xe3）经 Mesa ANV/Vulkan 已跑通锁定 14-fixture corpus，文本与 CPU FP32 199/199 一致；它属于现有开放兼容路径，只需补做 Provider Gate，不需要新代码（§9）。
- **后端只替换 `InferenceSession`。** 新实现位于 `src/inference/openvino/`，与 ONNX Runtime CPU/WebGPU、Direct Core ML 并列；预处理、DB postprocess、crop、CTC decode 与结果组装保持不变。
- **NPU 需要静态 shape。** Recognition 复用 Apple 路径已锁定的 20 个宽度桶，实测质量无损；Detection **不能**通过补边转成固定 shape（§3.3），必须按实际 shape 编译，或路由到 CPU。这是本方案最主要的设计取舍（§5）。
- **有 Intel NPU 时 Auto 优先使用 NPU。** Linux x64 与 Windows x64 的目标 Auto 顺序为 `openvino → webgpu → cpu`：主机没有可用 NPU 时 `openvino` 以 `adapter_unavailable` 跳过，行为与现状相同。用户仍可用 `provider=cpu|webgpu|openvino` 手动指定后端（§8）。按 D112，`openvino` 只有在随包交付并通过平台 Gate 后才进入 released policy；在此之前只能显式指定。
- **自包含分发，驱动为唯一前置条件。** OpenVINO Core、NPU plugin 与 TBB（合计约 28 MB）随 platform package 交付；NPU 内核驱动、用户态驱动与 compiler-in-driver 属于系统驱动栈，不打包（§7）。

## 2. 目标与非目标

### 2.1 目标

1. **释放 CPU。** 交互式 OCR 在 Core Ultra 笔记本上以 NPU 承担主要推理，进程 CPU-s 显著下降。
2. **端到端提速。** 以同一 corpus、同一 CPU 基线比较完整 OCR P50，而不是只比较单模型 microbenchmark。
3. **质量可审计。** 与 CPU FP32 locked goldens 对比文本、置信度和框；任何差异进入 parity exceptions 并经审阅。
4. **失败可解释。** 设备缺失、驱动过旧、编译失败都映射到 D112 封闭原因；运行期不跨 backend 重试。

### 2.2 非目标

- 不把 OpenVINO GPU plugin 作为 Intel GPU 路线；Intel GPU 由 WebGPU 覆盖。
- 不在同一 engine 内混用 WebGPU 与 NPU（§10）。
- 不在第一阶段做 INT8/QDQ 量化；NPU 编译器默认 FP16 推理精度。
- 不要求用户安装 OpenVINO SDK、Python 包或编译工具链，也不在 install/postinstall/首次运行时下载任何 runtime。
- 不因 NPU 占用率上升就宣称完整 placement；placement 以 OpenVINO `EXECUTION_DEVICES` 与编译结果为准。

## 3. 已确认事实与 Spike 证据

### 3.1 测试环境

| 项目 | 值 |
| --- | --- |
| CPU | Intel Core Ultra X7 358H（Panther Lake） |
| NPU | `Intel(R) AI Boost`，PCI `8086:B03E`，`DEVICE_ARCHITECTURE=5010`，FP16 25.2 TOPS / INT8 50.4 TOPS（`DEVICE_GOPS`） |
| GPU | Intel Arc B390（Xe3），Mesa ANV Vulkan |
| OS | Arch 系（Omarchy），kernel 7.2.5 |
| 驱动栈 | `intel-npu-driver 1.38.0`、`intel-npu-compiler 2026.38`（compiler in driver）、`level-zero-loader 1.32.0` |
| Runtime | 系统 `openvino 2026.4.0` + `openvino-intel-npu-plugin 2026.4.0`；light-ocr CPU 基线为 ONNX Runtime 1.22.0 |
| 模型 | `ppocrv6-small-onnx-20260714.2` 的上游 FP32 ONNX（det SHA-256 `d73e0058…`，rec `5435fd74…`） |

Spike 以临时 `InferenceSession` 实现替换 CPU 会话，由环境变量启用；其余 pipeline 未改。CPU 基线为 `cpu_fast` profile（12 intra-op 线程），每个 fixture 2 次 warmup + 10 次测量取 P50。**该基线与 WebGPU Gate 报告的 CPU 基线配置不同，倍数不可与 [Linux 报告](linux-device-acceleration.md#11-030-真实设备结论) 直接比较。**

### 3.2 单模型 microbenchmark

随机输入、固定 shape、20 次 P50：

| 模型 / shape | OpenVINO CPU | NPU | NPU 首次编译 | 与 CPU 输出最大绝对差 |
| --- | ---: | ---: | ---: | ---: |
| det 1×3×960×960 | 30.6 ms | 11.7 ms | 2.7 s | 0.0012 |
| rec 1×3×48×320 | 4.0 ms | 1.2 ms | 0.75 s | 0.042 |
| rec 1×3×48×1600 | 14.7 ms | 6.3 ms | 1.1 s | 0.039 |

`ov::cache_dir` 命中后，det 960×960 编译从 2.55 s 降到 15 ms。

### 3.3 完整 OCR（14-fixture corpus）

| 方案 | 14 个 P50 之和 | 相对 CPU | 文本一致 | 进程平均 CPU 核 |
| --- | ---: | ---: | ---: | ---: |
| CPU（ORT FP32） | 2,743 ms | 1.00× | 基线 | ≈11.8 |
| WebGPU FP32（Arc B390） | 1,622 ms | 1.69× | 199/199 | ≈0.5 |
| NPU：det 实际 shape + rec 20 桶 | 541 ms | 5.07× | 198/199 | ≈0.5 |
| det WebGPU + rec NPU（仅 Spike） | 757 ms | 3.62× | 199/199 | ≈0.4 |
| det CPU + rec NPU（由分阶段数据估算） | ≈1,035 ms | ≈2.65× | 待测 | 检测阶段回到多核 |

分阶段看，CPU 基线中 recognition 占推理时间约 78%（1,954 ms），detection 约 22%（553 ms）。以最重的 `paddleocr-xfund-form`（113 行）为例，CPU det/rec 为 68/955 ms，NPU 为 9/146 ms。

关键结论：

1. **Detection 补边破坏质量。** 输入在 normalize 后以 0 补边再裁剪输出，补到 960×960 时文本一致率降到 83/200，补到 64 的倍数时降到 172/199，补到 160 的倍数时同样出现多处框合并与文本差异；边界附近的框合并或新增，`paddleocr-boarding-pass` 甚至检出水印。Detection 必须使用实际 bounded shape。
2. **Recognition 宽度桶无损，但不得截断输出时间步。** 右侧补零到 20 个桶后，保留完整 `[N, T, C]` 交给 CTC decode，文本 198/199，与实际 shape 持平；若按宽度比例截断时间步，行尾字符会丢失。引擎已有的 `recognition_width_buckets_` 机制（Apple 路径使用）正好满足这一点。
3. **FP16 边界字符。** 每轮完整运行约有 1 行差异，且差异行不固定（`・`/`·`、`姓名NAME`/`姓名 NAME`），均为低置信边界字符。
4. **编译和缓存成本高。** 每个新 shape 首次编译 0.7–2 s；默认 cache blob 每个 5–15 MB，未设上限时 63 个 shape 达 685 MB；`CacheMode::OPTIMIZE_SIZE` 下 corpus 共 29 个 blob、167 MB。

### 3.4 Detection shape 空间

`bounded` 策略只缩小超过 960 的边，两边都向上取 32 的倍数，因此 detector 输入是 32–960 × 32–960 的任意 32 倍数组合，最多 **900 种 shape**，不是只有长边 960 的 59 种。corpus 实测即出现 `192×800`、`544×896`、`416×416`、`64×384`、`960×704` 等。按实际 shape 编译意味着多数新尺寸图片都要付一次编译成本。

### 3.5 分发相关事实

| 组件 | 大小 | 归属 |
| --- | ---: | --- |
| `libopenvino.so` | 19.2 MB | 随包 |
| `libopenvino_intel_npu_plugin.so` | 8.0 MB | 随包 |
| `libopenvino_c.so` | 0.3 MB | 随包（若采用 C API，§6.1） |
| `libtbb.so.12` | 0.25 MB | 随包 |
| `libopenvino_onnx_frontend.so` | 6.4 MB | 若离线转 IR 则不需要（§7.2） |
| `libopenvino_intel_npu_compiler.so` | 107 MB | 驱动栈（compiler in driver），不随包 |
| `libze_intel_npu.so`、`libze_loader.so` | — | 驱动栈，不随包 |

插件默认 `NPU_COMPILER_TYPE=PREFER_PLUGIN`；显式设为 `DRIVER` 时，本机仍能成功编译 recognizer，实际加载的是驱动栈中的 compiler loader。NPU plugin 的直接依赖为 `libopenvino`、`libtbb`、`libpugixml` 与 C/C++ 运行库。

## 4. 设备与精度矩阵

| 设备 | 路径 | 精度 | 状态 |
| --- | --- | --- | --- |
| Intel Core Ultra NPU（Linux x64） | Direct OpenVINO NPU | FP16（编译器默认） | 本方案第一目标 |
| Intel Core Ultra NPU（Windows x64） | Direct OpenVINO NPU | FP16 | 第二目标；Windows NPU 驱动随系统 Windows Update 分发 |
| Intel iGPU/dGPU | Native WebGPU（Vulkan/D3D12） | FP32 | 已有开放兼容路径；待 Provider Gate |
| Intel CPU | ONNX Runtime CPU | FP32 | 稳定基线，不变 |
| INT8/QDQ on NPU | — | — | 后续；需要独立校准与质量 Gate |

## 5. 路由设计

### 5.1 Recognition：NPU，20 个宽度桶

- batch 固定为 1，高度 48，宽度向上取到 Apple 路径已锁定的 20 个桶：`320 … 3200`。
- 超过最大桶的宽度处理与 Apple 路径一致，不引入新语义。
- 输出保持完整时间步，由现有 CTC decode 处理补齐区域产生的 blank。
- **内容宽度保持与未补齐张量一致。** 宽度按 32 取整或按桶补齐后，现有预处理会把文字内容缩放到 `ceil(48 × 宽高比)`，而 CPU batch-1 路径使用 `trunc(48 × 宽高比)`；每行内容宽 1 像素会系统性改变插值结果。OpenVINO 路径启用 `natural_content_width`，内容保持截断宽度、只在右侧补零；关闭时 14-fixture 文本一致为 192/199，开启后为 198/199。Apple 路径保持原有语义不变。
- 20 个编译产物构成有界集合，可预热（§6.3）。

### 5.2 Detection：两个候选，需 Gate 决定

| 候选 | 做法 | 优点 | 代价 |
| --- | --- | --- | --- |
| **D-NPU**：NPU 按实际 shape | 首次遇到的 shape 同步编译并写入有界 cache | 全部推理在 NPU，Spike 实测 5.07× | 新尺寸图片首张多 0.7–2 s；900 种 shape 需要 LRU 与磁盘上限 |
| **D-CPU**：CPU 执行 detector | OpenVINO 后端内部以 CPU 执行 detector（ORT CPU 会话或 OpenVINO CPU plugin，FP32） | 无 detector 编译成本，检测结果与 CPU goldens 对齐 | 估算约 2.65×；检测阶段仍占多核 |

两者都是**同一 `openvino` 候选内部的 stage 路由**，与 Apple 后端把 detector 与宽 recognition 分别路由到 ANE/MLCPU/GPU 的方式一致，不构成 D112 所禁止的混合 backend。无论选哪一种，实际设备都必须写入 `SessionExecutionInfo`（detector 的 `device`、`actualProviderChain`），不能隐藏 CPU 执行。

建议：Phase B 同时实现两种路由，并以 qualification-only 开关切换；由 Phase C Gate 按"冷启动 + 首张新尺寸图片延迟"与"稳态 P50 + CPU-s"两组指标选定默认。若 D-NPU 被选中，D-CPU 保留为 `cpuPartition=allow` 下 detector 的受控实现，`cpuPartition=forbid` 时只允许 D-NPU。

以下补边改进可作为 Phase C 的附加实验，但在 Gate 证明质量等价之前不得进入产品：以非零背景值或边缘复制补边、只补短边到少量桶、按桶重采样而不是补边。

### 5.3 不跨 backend 回退

创建成功后 backend 冻结；NPU 设备丢失、驱动重置或推理失败直接返回错误，符合 D112。D-NPU 首次编译失败属于运行期错误，不得静默改走 CPU。

## 6. 后端实现

### 6.1 代码结构与 runtime 装载

- `src/inference/openvino/backend.{hpp,cpp}`：`OpenVinoSession : InferenceSession`，持有每个 shape 的 `CompiledModel` 与 `InferRequest`。
- runtime 通过 `dlopen` 从 platform package 内的固定相对路径装载，不链接系统 OpenVINO，不搜索 `LD_LIBRARY_PATH`。优先使用 OpenVINO **C API**（`libopenvino_c`），避免 C++ ABI 与 addon 的 libstdc++ 版本耦合；若 C API 缺少所需属性，再评估 C++ API 与符号版本约束。
- 装载前校验 descriptor 中每个库的字节数与 SHA-256，与 WebGPU plugin 的做法一致。
- 与 ONNX Runtime 同进程共存：Spike 分别在 CPU flavor（ORT 1.22）与 WebGPU flavor（ORT 1.24.4 + WebGPU plugin）的进程中加载并运行 OpenVINO，均无冲突；产品实现仍需在 Gate 中覆盖 20 次 lifecycle。

### 6.2 Session 配置

| 属性 | 值 | 说明 |
| --- | --- | --- |
| device | `NPU` | 第一阶段不暴露 `deviceId`；多 NPU 主机不在范围内 |
| `PERFORMANCE_HINT` | `LATENCY` | 交互式 profile；`throughput` 后续评估 |
| `INFERENCE_PRECISION_HINT` | `f16` | 显式固定，不依赖默认值 |
| `NPU_COMPILER_TYPE` | `DRIVER` | 不随包分发 107 MB 编译器 |
| `CACHE_DIR` | 产品缓存目录（§6.3） | |
| `CACHE_MODE` | `OPTIMIZE_SIZE` | 实测 blob 显著变小；需确认权重来源始终是随包模型 |
| `NPU_TURBO` | 默认 `NO` | 功耗优先；是否开启由 Gate 评估 |

### 6.3 编译缓存

- 目录沿用 Apple 编译缓存的根目录约定，键包含：模型 SHA-256、OpenVINO 版本、`NPU_DRIVER_VERSION`、`NPU_COMPILER_VERSION`、`DEVICE_ARCHITECTURE`、shape。任一变化即视为新条目。
- 跨进程写入使用与 Apple 缓存相同的文件锁语义；损坏条目删除后重编译，不作为 creation failure。
- 总大小设上限（初值 256 MiB，Gate 校准），LRU 淘汰；recognition 的 20 个桶优先保留。
- Engine 创建时只同步编译最常用的 recognition 桶与 hello canary 所需 shape，其余桶在后台线程预热；后台编译不得阻塞 `recognize()`，也不得在 engine 关闭后继续写入。
- `SessionExecutionInfo.model_cache_status` 报告 `hit | miss | disabled`。

### 6.4 D112 创建原因映射

| 情况 | 原因 |
| --- | --- |
| `/dev/accel` 不存在、OpenVINO 未枚举到 `NPU` | `adapter_unavailable` |
| `NPU_DRIVER_VERSION` 或 compiler 版本低于 descriptor 锁定下限 | `driver_version_unsupported` |
| 编译 locked 模型/shape 失败（算子或 shape 不支持） | `model_compute_unsupported` |
| NPU plugin 明确报告设备内存不足 | `device_memory_insufficient` |
| descriptor 声明的库缺失或结构无效 | `package_corrupt` |
| 库哈希不符 | `artifact_hash_mismatch` |
| OpenVINO 版本与 plugin/descriptor 不符 | `provider_abi_mismatch` |
| 其他装载失败 | `unrecoverable_load_failed` |

分类只来自 typed 状态与版本比较，不解析异常文本。

## 7. 模型产物与分发

### 7.1 Platform package

- `@arcships/light-ocr` 的 Linux x64 glibc（及后续 Windows x64）platform package 增加 `openvino/` 目录：Core、NPU plugin、TBB 与许可证/SBOM；约 28 MB，未超出 Linux Gate 的 256 MiB 解包 native payload 上限，但需要 package review 接受包体增长。
- runtime descriptor 新增 `openvino` provider 条目：库清单与哈希、OpenVINO 版本、最低 NPU driver/compiler 版本、qualification ID。
- musl 与 arm64 不在范围内；Intel 官方 NPU 驱动只面向 glibc x64。
- Level Zero loader 视为驱动栈组件；若 Gate 发现主流发行版默认不带，再单独决定是否随包。

### 7.2 模型形式

两种选择，Phase B 决定：

1. 直接读取已锁定的上游 FP32 ONNX：需要随包 `libopenvino_onnx_frontend`（+6.4 MB），但不新增模型产物。
2. 构建期离线转换为 OpenVINO IR（`.xml` + `.bin`，可 FP16 压缩）：不需要 ONNX frontend，加载更快；但它是新的 immutable 派生物，需要 provenance、确定性再生与 bundle manifest schema 升级，规则同 D113 的 WebGPU FP16 派生物。

建议先用方式 1 完成 Gate，确认收益后再评估方式 2。

## 8. 公共 API 与可观测性

- `ExecutionProvider` 增加 `openvino`；Node 类型 `provider?: 'cpu' | 'auto' | 'apple' | 'webgpu' | 'openvino'`，与 roadmap 中预留的名称一致。
- 显式 `provider=openvino` 只尝试该后端；在未交付的平台上返回 `unsupported_capability`。
- `precision` 只接受 `auto` 与 `fp16`；`fp32` 返回 `invalid_argument`。
- `cpuPartition=forbid` 只在 detector 路由为 D-NPU 时有效，否则在创建前拒绝。
- `SessionExecutionInfo`：`runtime=OpenVINO`、`runtime_version`、`device=npu:<FULL_DEVICE_NAME>`、`device_family=<DEVICE_ARCHITECTURE>`、`precision=fp16`、`shape_policy`、`model_cache_status`。
- Per-call recognition diagnostics 复用现有 `shape_bucket` 与 `compute_unit=npu` 字段。
- `light-ocr doctor --json` 报告 NPU 是否存在、驱动与 compiler 版本、是否满足 descriptor 下限；不上报设备 UUID。

### 8.1 Auto 与手动指定

目标 D112 policy（新 policy ID/version，以新的 D 编号记录）：

| 平台 | 当前 | 目标 |
| --- | --- | --- |
| Linux x64 glibc | `webgpu → cpu` | `openvino → webgpu → cpu` |
| Windows x64 | `webgpu → cpu` | `openvino → webgpu → cpu` |
| 其他平台 | 不变 | 不变（不交付 `openvino`） |

- **有 NPU 就用 NPU。** Auto 在创建时先尝试 `openvino`；没有 NPU、驱动低于下限或编译失败时，分别以 `adapter_unavailable`、`driver_version_unsupported`、`model_compute_unsupported` 跳过，继续尝试 `webgpu`，最后是 `cpu`。包损坏、哈希不符等 fatal 原因仍按 D112 立即终止。
- **NPU 优先于独显是明确的产品取舍。** 同时具备 NPU 与独显的主机，Auto 也选择 NPU，理由是省电、释放 CPU/GPU 给前台负载；需要最高吞吐的用户手动指定 `provider=webgpu`。
- **手动指定。** `provider=cpu|webgpu|openvino` 只尝试指定后端，失败直接返回结构化错误，不回退。Node、C++ 与 CLI（`--provider`）使用同一组取值。
- **进入 released policy 的条件。** 按 D112，`openvino` 必须由 runtime descriptor 声明、随 platform package 交付、并通过该平台 Gate 后才进入 released policy；在此之前 released policy 保持 `webgpu → cpu`，`provider=openvino` 仅在包含该 runtime 的构建中可显式使用。
- **失败候选的清理与共存。** `openvino` 创建失败后，必须先销毁全部 OpenVINO 状态（compiled model、infer request、Core）再尝试 `webgpu`；OpenVINO 与 ORT WebGPU 需在同一进程中共存。两者都要在 Gate 中覆盖（§11），否则按 D112 不得同列一个 Auto list。
- **Auto 下的创建延迟。** Auto 选中 NPU 意味着默认用户也会承担 NPU 的编译成本，因此 engine 创建不得同步编译全部 shape：只同步准备 canary 所需的最少 shape，其余在后台预热（§6.3）。这也使 detector 路由（§5.2）的"首张新尺寸图片延迟"成为默认体验指标。

## 9. Intel GPU：WebGPU 兼容路径

本机实测结论：

- 锁定 WebGPU runtime（ORT 1.24.4 + WebGPU plugin 0.1.0）经 Dawn/Vulkan 选中 Intel 适配器（vendor `0x8086`），`webgpu_allow` profile 下 14 个 fixture 文本 199/199 一致，置信度最大差 1e-4。
- 相对 `cpu_fast` 为 1.69×；recognition 是瓶颈（xfund-form 689 ms，NPU 为 146 ms）。

后续动作不涉及新代码：在 Arc B390 上运行 `tools/webgpu/qualify.py` 取得完整 164 项 Gate 报告，审阅后按 Linux 文档规则补充 Intel 设备行；在此之前 Intel GPU 仍属开放兼容路径，不继承已发布的性能倍数。

## 10. 已评估并否决的方案

| 方案 | 结论 | 依据 |
| --- | --- | --- |
| WebGPU 直接使用 NPU | 不可行 | NPU 无 Vulkan/D3D12 适配器 |
| det WebGPU + rec NPU（同一 engine 混合 backend） | 不采用 | 199/199 但仅 3.62×，慢于全 NPU；需要同进程加载两套 runtime，并违反 D112"候选拥有完整 detector/recognizer 对、不暴露混合 backend"的约束 |
| 多页流水线（GPU 检测下一页、NPU 识别本页） | 暂不采用 | 每页耗时由 recognition 决定（xfund ≈157 ms），与全 NPU（≈155 ms）相当 |
| recognition 行级拆分到 GPU + NPU | 不采用 | GPU recognition 慢约 4.5×，理论收益 ≤20%，同页混合 FP16/FP32 使质量不可复现 |
| OpenVINO `HETERO` 层级切分 | 不采用 | 小模型跨设备拷贝得不偿失 |
| OpenVINO `AUTO` / GPU plugin 统一调度 | 不采用 | 绕开已验收的 WebGPU 路线，且需要额外 GPU plugin |
| ORT OpenVINO EP | 不采用 | 需要替换当前官方 ORT 构建；NPU 静态 shape 仍需自行管理 |
| detector 固定尺寸补边 | 否决 | §3.3，质量显著下降 |

## 11. Gate

沿用 Linux WebGPU Gate 的结构，增加 NPU 特有项：

- **质量**：14-fixture locked corpus 与 CPU FP32 goldens 对比；文本不一致行数不超过审阅后的 parity exceptions，框最大偏差与置信度差设上限；FP16 边界差异须逐条审阅。
- **性能**：每个 case 3 次独立 cold start × (2 warmup + 10 次测量)；报告 P50、P95、各阶段时间、进程 CPU-s 与 RSS。
- **冷启动**：`generated-hello-123` canary 在空缓存下 engine 初始化 + 首个结果 ≤30 s，热缓存 ≤3 s（D111 口径）；另记录"新尺寸图片首张延迟"（D-NPU）。
- **缓存**：总大小不超过上限；并发两进程写同一缓存无损坏；驱动版本变化后正确失效。
- **生命周期**：20 次 engine close/recreate，retained RSS 增长绝对值 ≤128 MiB；NPU 句柄无泄漏。
- **失败路径**：无 NPU 主机、`/dev/accel` 权限不足、驱动过旧、库缺失/哈希错误分别产生 §6.4 中的原因。
- **Auto**：有 NPU 主机选中 `openvino`；无 NPU 主机 trace 为 `openvino skipped(adapter_unavailable) → webgpu selected`；`openvino` 失败后清理完整、`webgpu` 正常创建；手动指定 `webgpu`/`cpu` 时不加载 OpenVINO。
- **分发**：离线复装、无网络运行、无系统 OpenVINO 时可用；有系统 OpenVINO 时仍使用随包版本。

## 12. 分阶段落地

### Phase A — 证据（已完成）

Spike 数据见 §3；临时代码不合入主干。

### Phase B — 后端实现（qualification build）

1. `OpenVinoSession`、dlopen 装载、C API 封装、哈希校验。
2. Recognition 20 桶路由；detector D-NPU 与 D-CPU 两种路由。
3. 有界编译缓存与跨进程锁、后台预热。
4. `ExecutionProvider::openvino`、C++/Node 参数校验、diagnostics。
5. CMake 选项 `LIGHT_OCR_OPENVINO_SDK_DIR` 与锁定的 OpenVINO payload（参照 `tools/webgpu/runtime-lock.json` 的模式）。
6. 单元测试：shape 桶、cache key、原因映射；CI 在无 NPU runner 上验证 `adapter_unavailable` 路径。

### Phase B 实施状态（2026-09-29）

已实现：

- `ExecutionProvider::openvino`；C++ 校验只接受 `precision=auto|fp16`、无 `deviceId`；Node addon、TypeScript 类型、runtime CLI、document CLI 与 server `EXECUTION_MODE` 接受 `openvino`。
- `src/inference/openvino/backend.{hpp,cpp}`：`dlopen` 装载 OpenVINO C API，按 shape 编译并在内存中 LRU 保留（detector 32、recognizer 20），编译参数为 `LATENCY`、`f16`、`NPU_COMPILER_TYPE=DRIVER`，磁盘缓存为 `OPTIMIZE_SIZE`。
- 磁盘缓存位于 `$XDG_CACHE_HOME`（或 `~/.cache`）`/com.arcships.light-ocr/openvino-v1/<identity>`，identity 为模型 SHA-256、OpenVINO 版本、设备架构、驱动与 compiler 版本的哈希；编译在跨进程 `flock` 下进行，写入后按修改时间淘汰到 256 MiB。
- Detector 路由：默认 D-NPU；qualification build 可用 `LIGHT_OCR_QUALIFICATION_OPENVINO_DETECTOR=cpu` 切换到 D-CPU，此时 `cpuPartition=forbid` 在创建前以 `model_compute_unsupported` 拒绝。
- D112：构建时设置 `LIGHT_OCR_OPENVINO_SDK_DIR` 后，builtin policy 为 `builtin-openvino-v1`，顺序 `openvino → [webgpu →] cpu`，并标记 qualification-only。batch≠1、非 bounded、`maxSide>960` 或不兼容的 recognizer 形状以 `model_compute_unsupported` 跳过。
- 工具 profile `openvino_allow` / `openvino_strict`；单元测试覆盖参数校验、policy 校验、builtin 顺序、宽度桶契约与内容宽度语义。

本机结果（Core Ultra X7 358H，OpenVINO 2026.4，NPU driver 1.38）：

| 项目 | 结果 |
| --- | --- |
| 14-fixture 文本一致 | 198/199（唯一差异 `姓名NAME`/`姓名 NAME`） |
| 14 个 P50 之和 | 506–573 ms（4 次运行），相对 `cpu_fast` 4.8–5.4× |
| hello canary 冷缓存 init + 首个结果 | 2.1 s + 1.3 s |
| hello canary 热缓存 init + 首个结果 | 0.12 s + 0.05 s |
| 20 次 engine lifecycle | RSS 增长 86 KiB（上限 32 MiB） |
| 无 NPU（bwrap 隐藏 `/dev/accel`）Auto | `openvino skipped(adapter_unavailable) → webgpu selected` |
| 显式 `webgpu` | 进程不加载 OpenVINO |

尚未实现（留在 Phase B/C）：后台预热其余 recognition 桶、按 descriptor 的驱动/compiler 最低版本检查（`driver_version_unsupported`）、LRU 中优先保留 recognition 桶、`model_cache_status` 的 hit/miss 区分、Node runtime descriptor 中的 OpenVINO 条目（Phase D）。

### Phase C — Linux x64 真机 Gate

在至少两代 Core Ultra（Meteor Lake / Lunar Lake 或 Arrow Lake / Panther Lake）上跑 §11，选定 detector 默认路由，写入审阅报告。

### Phase D — 分发

platform package staging、SBOM、许可证、runtime descriptor、package review；`provider=openvino` 以 Preview 发布。

### Phase E — Windows x64

同一后端在 Windows NPU 驱动上复用；分发 `openvino.dll` 等，重跑 Gate。

### Phase F — 进入 Auto

记录新的 D 编号，确立 Linux/Windows `openvino → webgpu → cpu` policy；平台 Gate 通过、报告与产物哈希绑定 lock 后，更新 released policy，并在 CHANGELOG 中说明 Intel NPU 主机上 Auto 的选择变化。

## 13. 待决问题

1. Detector 默认走 D-NPU 还是 D-CPU（§5.2）？
2. 模型形式：直接读 ONNX 还是离线 IR 派生物（§7.2）？
3. 约 28 MB 的 platform package 增长是否接受？是否需要拆出可选 package（需同时符合"用户只安装 `@arcships/light-ocr`"的约束）？
4. Level Zero loader 是否视为驱动前置条件？
5. OpenVINO 版本升级节奏与 NPU 驱动兼容下限如何锁定？
6. 若某代 NPU 在 Gate 中慢于同机 WebGPU，是按 device family 在 descriptor 中排除该代，还是保持"有 NPU 即优先"？

## 14. 参考

- [OpenVINO NPU device](https://docs.openvino.ai/2026/openvino-workflow/running-inference/inference-devices-and-modes/npu-device.html)
- [OpenVINO model caching](https://docs.openvino.ai/2026/openvino-workflow/running-inference/optimize-inference/optimizing-latency/model-caching-overview.html)
- [Intel NPU driver for Linux](https://github.com/intel/linux-npu-driver)
- [ONNX Runtime OpenVINO EP](https://onnxruntime.ai/docs/execution-providers/OpenVINO-ExecutionProvider.html)
