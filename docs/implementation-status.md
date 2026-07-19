# C++ Core 与 Node-API 实施状态

更新时间：2026-07-19<br>
结论：`@arcships/light-ocr@0.2.0` 已发布并提升为 npm `latest`。当前 0.3.0 发布候选已实现 Direct Core ML Apple provider，以及 Linux x64 glibc/Windows x64 official Native WebGPU Plugin EP 的产品 runtime、D112 Auto、自包含 npm payload 与资格工具。Apple M4 已有审阅证据；WebGPU 的 Linux/Windows 真实设备报告均为 164/164 Gate 通过，已审阅报告和产物哈希已绑定 production lock。

状态含义：

- **Done**：代码存在，并有本地实际运行证据。
- **Configured**：自动化已写好，但当前工作区未产生真实远端 run 证据。
- **Pending**：需要外部平台或制品仓动作。

## 需求验收矩阵

| `requirements.md` §19 条目 | 状态 | 当前证据或缺口 |
| --- | --- | --- |
| 四个 Tier 1 原生构建/测试 | Done | Release commit 的 [Core run 29312484043](https://github.com/arcships/light-ocr/actions/runs/29312484043) 六个 jobs 全部成功。 |
| 生产 Core 无 Python、无子进程 | Done | `light_ocr_core` 仅 C++；Python 只在 oracle/generator/report tools；Core 无 process/shell API。 |
| raw-pixel 公共 API、ownership/lifecycle 文档 | Done | `include/light_ocr/*.hpp` 与 [native-api.md](native-api.md)。 |
| detection/geometry/crop/recognition/decode 分层与测试 | Done | 独立源码模块、unit tests、stage probe 和真实模型 integration tests。 |
| PP-OCRv6 bundle 固定、哈希、许可、离线可用 | Done（0.2.0 published） | `.2` 使用相同受控 ONNX bytes，发布 schema 1.2、`tiled-v1` contract、新 manifest/config/archive hash，并把 minimum Core 提升到 0.2.0；`.1`/schema 1.1 证据保持不变。 |
| stage 与 final parity | Done | `upstream_exact` 与 `bounded_default` 均为 14/14；候选级 trace 完整；release commit 的 oracle 与四平台 jobs 全绿。 |
| 首 bundle ground-truth quality report | Done（本机） | bounded 默认在 10 个锁定 fixtures 上 10/10 exact、CER `0`；IoU≥0.5 下 detection precision/recall/Hmean 均为 `1.0`。旧 exact 基线仍独立保留。 |
| 相对性能门槛 | Done（参考本机） | bounded 默认：median `0.9824867× ≤ 1.10×`；p95 `1.0139793× ≤ 1.15×`；inference median `0.9961966× ≤ 1.05×`。受控 CI worker 报告仍应保留。 |
| Sanitizer、fuzz、leak、lifecycle、malformed input | Done | 本机 ASan+UBSan、TSan、standalone fuzz、lifecycle 和 malformed model/tensor 已通过；release Core safety job 的 sanitizers、TSan 和 libFuzzer smoke 全绿。 |
| 无 network/shell/cwd/locale 运行依赖 | Done | sterile cwd/minimal env 与 Linux network namespace disabled 测试通过；npm release 另完成已安装 package 的禁网运行。 |
| manifest、hash、licenses、SBOM、parity、benchmark | Done | Release commit 已重新生成并保存四平台 metadata、六个 npm tarballs 的 hashes/integrity、parity、quality 与 benchmark 证据。 |
| N-API/npm 非本 Core milestone | Done / `0.2.0` published | raw Node-API v8、CJS/ESM、`.d.ts`、内置模型解析、四平台 prebuild、双重背压、AbortSignal 与生命周期均已完成；[npm release run 29340467784](https://github.com/arcships/light-ocr/actions/runs/29340467784) 与 [promotion run 29342178842](https://github.com/arcships/light-ocr/actions/runs/29342178842) 保存六包发布、registry 和禁网证据。 |
| Perf-1A / Apple execution | Done locally / open macOS | provider-neutral `InferenceSession` 已加入 Objective-C++ Direct Core ML；公开 union 与 D112 Auto 创建状态机已接线。detector 使用 FP16 range model，recognizer 使用 91-function FP16 MLProgram 和 20 个加权宽度桶；Apple Silicon interactive 为 ANE + 宽文本 GPU，strict 为 GPU，Intel 为 CPU+GPU。schema 1.1 provider contract 使用 `open-macos`、arm64/x86_64、`validatedDeviceFamilies` 和 `deviceValidated`；显式 provider 严格失败，只有 Auto 可按 typed reason 在创建期继续。哈希锁模型、离线编译缓存、跨进程锁、LRU≤20 与 Node 映射均已完成。M4 有正式证据，其他 Mac 直接开放实验兼容。 |
| Perf-2 / Native WebGPU | Production-qualified / two device Gates passed | Linux x64 glibc/Vulkan 与 Windows x64/D3D12 使用 official ORT Core 1.24.4 + WebGPU Plugin EP 0.1.0。NuGet bytes/SHA-512、headers、runtime/plugin/companions、license 和 session options 已锁定；assembler 支持在线取得、离线复装和 exact SDK 校验。C++/Node plugin registration、D112 `webgpu → cpu`、typed/fatal failure、FP32 allow/strict、真实 provider chain、profiling、schema 2 descriptor、sterile loader、self-contained npm staging、license/SBOM 和双平台 CI 已实现。Linux RTX 5060 Ti/Vulkan 与 Windows Radeon 780M/D3D12 报告均为 164/164 Gate 通过、14/14 FP32 字节级质量对齐；聚合 P50 分别加速 5.698× 与 2.436×。已审阅 report/artifact hashes 已绑定 production lock，release configure 仅接受精确匹配的 SDK。 |
| Node.js JPEG/PNG 内存输入 | Done / `0.2.0` published | `recognizeEncoded(Uint8Array)` 在 engine worker 上使用固定 stb revision 解码，保持 Core raw-pixel 边界；格式、尺寸、pixels、临时内存、queue/snapshot budget、AbortSignal 与 `timingUs.decode` 均有四平台 Node 22/24 package 测试。 |
| 高分辨率峰值内存 | Done | Release 原生独立进程本机参考：2048² 空白 `318.8 MiB ≤ 384 MiB`；xfund 密集表单 116 框 `400.5 MiB ≤ 640 MiB`。四平台 release jobs 的真实模型与 RSS gates 均通过。 |
| Tiled 高分辨率准确模式 | Done / `0.2.0` published | 1280 tile、2048→4-pass row-major、全局 candidate ceiling、IoU/IOS greedy merge、原图 recognition、C++/Node contract、8-fixture/196-line corpus、独立 oracle、四平台 36-entry accepted baseline 与 package smoke 均已完成。 |

## 本机最终验证快照

环境：macOS arm64 Apple M4 Max，macOS 26.5.1，Apple Clang 21.0.0，CMake 4.2.1，macOS deployment target 13.3；CPU 使用 ONNX Runtime，Apple 候选使用系统 Core ML。

| 验证 | 结果 |
| --- | --- |
| Release acceptance CTest | 15/15 passed；包括旧双 profile golden/parity/quality、八图 tiled lock/parity/quality，以及 bounded/tiled 原生 memory gate |
| 全阶段语料 | `upstream_exact` 14/14；`bounded_default` 14/14 |
| 质量基线 | bounded 10/10 exact；0/104 CER；10 TP / 0 FP / 0 FN，detection P/R/Hmean = 1.0（IoU≥0.5） |
| ASan + UBSan | 2/2 passed；Apple 平台不启用 LSan |
| TSan | 2/2 passed |
| standalone fuzz | image 100k、bundle 100k、geometry 100k、lifecycle 10，全部完成 |
| leak/RSS | 5 warmup + 10 cycles；当前复测 growth 21,413,888 bytes（2,141,388 bytes/cycle）；gate 32 MiB / 8 MiB per cycle |
| offline contract | sterile cwd/minimal locale environment passed |
| model archive | 已发布 `.1`：31,334,400 bytes / `74e246bf…de17`；已发布 tiled `.2`：31,334,400 bytes / `e543b93b…712f` |
| Node-API v1 | Node.js 22.13.0；macOS arm64 Release/Werror 构建；CTest 3/3；bounded/exact 映射、真实 PP-OCRv6 API、snapshot/byteOffset、校验、symlink root、双重背压、abort、heartbeat、close/worker teardown 测试通过 |
| Perf-1A local validation | macOS arm64 Release/Werror 构建；Apple Release CTest 7/7、Node 绑定 16/16、Python Apple/npm 合约 13/13；CPU 默认结果不变，逐 session execution summary、未知 provider、FP16、device ID 和无效 fallback 组合均有 C++/Node integration 覆盖 |
| Apple model placement | detector interactive 为 190 ANE + 2 个已声明 MLCPU 操作，strict 为 192 GPU；recognition 91/91 宽度函数全部通过，宽区间 213 GPU 且无 MLCPU；detector/recognizer 包哈希 `2097bd78…7f76` / `c54a0719…5f4b`，`.2` 报告 `f9b4cfdb…d983` |
| Apple quality | 14 fixtures 全部通过；字符相似度 99.6484%，detection recall 100%，平均 IoU 99.5508%，平均置信度差 0.004349，critical failure 0；`.2` 报告 `79d5b9f6…6ae1` |
| Apple performance | hello / xfund warm P50 为 8.599 / 331.011 ms，相对 CPU-fast 加速 2.300× / 2.851×，CPU time 降低 95.91% / 97.67%；canary cold cache miss 7.219 s、hit 1.275/1.278 s；warm peak RSS 最大 692.14 MiB，bundle 增量 25.42 MiB；`.2` 报告 `cecf7607…cd8d` |
| Apple cache concurrency | 4 进程竞争通过；detector/recognizer 各恰好一个 miss、3 个 hit，结果哈希一致且无临时目录残留；`.2` 报告 `8356c20c…2f64` |
| Apple 100-page lifecycle | 同一 interactive engine 预热 2 页后连续处理 100 个 xfund 密集页；RSS baseline/final/maximum 为 887.27/859.80/888.09 MiB，growth -27.47 MiB，通过 32 MiB 工具门槛和 64 MiB acceptance；`.2` 报告 `f695157a…6195` |
| Apple policy fallback | 本机以测试专用 `validated-only` 策略排除 M4 时，detector/recognizer 均稳定落到 ONNX Runtime CPU，原因 `apple_device_unqualified`，canary 保持 `HELLO 123`；生产 `open-macos` 不执行该拦截；历史报告 `2e72ab7e…d823` |
| Apple provider baseline | qualification `apple-fp16-mixed-20260715.2` 已接受；`Apple M4` 是当前唯一 validated evidence，而非运行 allow-list；candidate/accepted 自哈希链完整，accepted 报告 `5ac8e117…2788` |
| Linux Native WebGPU | NVIDIA RTX 5060 Ti / Vulkan；164/164 Gate；14/14 FP32 与 CPU 字节级一致；CPU/WebGPU P50 总和 5,475.623/961.042 ms，聚合 5.698×，单项 3.474×–9.299×；native payload 41.4 MiB |
| Windows Native WebGPU | AMD Radeon 780M / D3D12；164/164 Gate；14/14 FP32 与 CPU 字节级一致；CPU/WebGPU P50 总和 6,500.853/2,669.160 ms，聚合 2.436×，单项 1.277×–2.982×；warmup-aware lifecycle -22.9 MiB，native payload 44.1 MiB |
| Tiled corpus | 八张 2048² locked fixtures 共 196 行：196 TP / 0 FP / 0 FN、CER 0、duplicate line 0；独立 oracle 与原生 pass tensor、candidate source、suppression、representative、crop、decode 和 final order 对齐；side override、tile ceiling、global candidate ceiling 均返回稳定错误 |
| Tiled qualification | [run 29336329115](https://github.com/arcships/light-ocr/actions/runs/29336329115) 四个平台采样 jobs 成功；36 个 Core/Node 22/Node 24 entries 已受审。各平台最大 Core/Node 峰值：Linux x64 639.7/715.6 MiB、Windows x64 616.1/667.5 MiB、macOS arm64 667.4/733.6 MiB、macOS x64 623.1/672.8 MiB |

性能报告（5 warmup + 30 iterations，`generated-hello-123`）：

| 指标 | Native | Python oracle | 比率 | 门槛 |
| --- | ---: | ---: | ---: | ---: |
| warm median end-to-end | 75,678 µs | 77,027 µs | 0.9824867× | ≤ 1.10× |
| warm p95 end-to-end | 79,788 µs | 78,688 µs | 1.0139793× | ≤ 1.15× |
| inference-only median | 74,125 µs | 74,408 µs | 0.9961966× | ≤ 1.05× |

这些数值是这台机器上的验收快照，不是所有硬件的绝对性能承诺。

## 发布结论与后续范围

`0.2.0` 的四平台 Core、Node.js 22/24 prebuild、六包确定性制品、public registry、provenance、默认 `createEngine()`、显式 tiled 和禁网运行证据已经完成，详见 [npm 0.2.0 发布记录](releases/npm-0.2.0.md)。

普通 push、PR 和 release workflow 均不运行 benchmark。后续只有 Core/model/ORT/compiler/thread policy/runner class 变化、准备公开新性能数字或调查疑似回归时，才显式重新运行 qualification 并 review 新 baseline。0.1.0 的历史记录与制品保持不变。
