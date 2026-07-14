# C++ Core 与 Node-API 实施状态

更新时间：2026-07-14  
结论：C++ Core 功能实现、可重建语料、完整首 bundle 质量基线和 macOS arm64 本地验证已完成；Node-API v1 源码实现及 Node.js 22/macOS arm64 本地真实模型测试已完成。整个项目仍不能宣称发布验收完成，因为 Core 四平台 CI、Node 22/24 四平台预编译矩阵、Linux 专属安全证据和模型受控镜像仍待外部执行。

状态含义：

- **Done**：代码存在，并有本地实际运行证据。
- **Configured**：自动化已写好，但当前工作区未产生真实远端 run 证据。
- **Pending**：需要外部平台或制品仓动作。

## 需求验收矩阵

| `requirements.md` §19 条目 | 状态 | 当前证据或缺口 |
| --- | --- | --- |
| 四个 Tier 1 原生构建/测试 | Configured / Pending | GitHub Actions matrix 已覆盖 macOS arm64/x64、Windows x64、Linux x64；本地只实际通过 macOS arm64。工作区不是 Git 仓库，尚无 Actions run URL/artifact。 |
| 生产 Core 无 Python、无子进程 | Done | `light_ocr_core` 仅 C++；Python 只在 oracle/generator/report tools；Core 无 process/shell API。 |
| raw-pixel 公共 API、ownership/lifecycle 文档 | Done | `include/light_ocr/*.hpp` 与 [native-api.md](native-api.md)。 |
| detection/geometry/crop/recognition/decode 分层与测试 | Done | 独立源码模块、unit tests、stage probe 和真实模型 integration tests。 |
| PP-OCRv6 bundle 固定、哈希、许可、离线可用 | 部分 Done | 原始归档、成员、dictionary、manifest、USTAR archive 均已锁定；最终 archive SHA-256 为 `d320…00da`。受控镜像仍是 `mirror: null`，因此“mirrored”尚未完成。 |
| stage 与 final parity | Done（本机）/ Pending（Tier 1） | macOS arm64 14/14；候选级 trace 完整；仅 PX-0001 窄范围例外。其他平台待 CI。 |
| 首 bundle ground-truth quality report | Done（本机） | 10 个锁定 fixtures、10 个独立框标注；9/10 exact，CER `0.0096153846`；IoU≥0.5 下 detection precision/recall/Hmean 均为 `1.0`。这是有限语料的首基线，不是一般化产品准确率声明。 |
| 相对性能门槛 | Done（参考本机） | median `0.9869991× ≤ 1.10×`；p95 `0.9790358× ≤ 1.15×`；inference median `1.0006504× ≤ 1.05×`。受控 CI worker 报告仍应保留。 |
| Sanitizer、fuzz、leak、lifecycle、malformed input | Done（本机可用部分）/ Configured | ASan+UBSan 2/2；TSan 2/2；Apple standalone fuzz image/bundle/geometry 各 100k、lifecycle 10；RSS 10 cycles 增长 65,536 bytes；损坏 ONNX/模型契约/ORT shape error 已测。Linux LSan 与真正 libFuzzer 待 CI。 |
| 无 network/shell/cwd/locale 运行依赖 | Done（sterile）/ Configured（network namespace） | 本地 sterile cwd+minimal env 两次结果一致；Linux `unshare --net` job 已配置，待真实 run。 |
| manifest、hash、licenses、SBOM、parity、benchmark | Done（本机）/ Pending（Tier 1） | 本地生成成功；当前无 Git revision，使用 source snapshot SHA-256。四平台 release metadata 待 CI。 |
| N-API/npm 非本 Core milestone | 源码实现 Done / 发布 Pending | `bindings/node` 已有 raw Node-API v8 addon、CJS/ESM facade、`.d.ts`、安全 bundle loader、专用 FIFO worker、双重背压、输入快照、AbortSignal、close/GC/environment teardown 和真实模型测试；尚无四平台 prebuild、npm registry 发布或 Node 24 平台证据。该工作不改变 Core milestone 的验收边界。 |

## 本机最终验证快照

环境：macOS arm64，Apple Clang 21.0.0，CMake 4.2.1，macOS deployment target 13.3，ONNX Runtime CPU，intra/inter-op threads 均为 1。

| 验证 | 结果 |
| --- | --- |
| Release CTest | 全新 build tree 9/9 passed（仅使用锁定依赖缓存；含 oracle golden 逐字节再生、ground-truth lock 和 contract mapping 验证） |
| 全阶段语料 | 14/14 passed |
| 质量基线 | 9/10 exact；1/104 CER；10 TP / 0 FP / 0 FN，detection P/R/Hmean = 1.0（IoU≥0.5） |
| ASan + UBSan | 2/2 passed；Apple 平台不启用 LSan |
| TSan | 2/2 passed |
| standalone fuzz | image 100k、bundle 100k、geometry 100k、lifecycle 10，全部完成 |
| leak/RSS | 2 warmup + 10 cycles；growth 65,536 bytes（6,553 bytes/cycle）；gate 32 MiB / 8 MiB per cycle |
| offline contract | sterile cwd/minimal locale environment passed |
| model archive | 31,334,400 bytes；SHA-256 `d320b799ed77511e3743c36d2f23bd8cbcd80d8070d5431f4fb0ec80daa800da` |
| Node-API v1 | Node.js 22.13.0；macOS arm64 Debug/Werror 构建；真实 PP-OCRv6 API、snapshot/byteOffset、校验、symlink root、request/byte 双重背压、queued/running abort、event-loop heartbeat、close drain、worker teardown 测试通过 |

性能报告（5 warmup + 30 iterations，`generated-hello-123`）：

| 指标 | Native | Python oracle | 比率 | 门槛 |
| --- | ---: | ---: | ---: | ---: |
| warm median end-to-end | 73,716 µs | 74,687 µs | 0.9869991× | ≤ 1.10× |
| warm p95 end-to-end | 74,347 µs | 75,939 µs | 0.9790358× | ≤ 1.15× |
| inference-only median | 72,305 µs | 72,258 µs | 1.0006504× | ≤ 1.05× |

这些数值是这台机器上的验收快照，不是所有硬件的绝对性能承诺。

## 发布前必须补齐

1. 将当前目录纳入 Git，触发并保存 `.github/workflows/core.yml` 的四平台、safety、oracle run。
2. 将确定性模型归档上传到批准的不可变制品仓，把 `models/bundles.lock.json` 的 `mirror` 从 `null` 更新为已验证地址。
3. 保存 Linux LSan、真正 libFuzzer 和 network namespace disabled 的报告。
4. 用带 Git revision 的干净 snapshot 生成四个平台的 build manifest、license inventory、SPDX SBOM 和 artifact hashes。
5. 为 Node.js 22/24 在四个 Tier 1 平台生成 prebuild，完成 compatible-host sanitizer、worker termination、leak 与性能门槛，并保存不可变测试证据。

在以上事项完成前，应称为“C++ Core 实现完成、发布验收待补”，而不是“整个 milestone 已发布完成”。
