# C++ Core 与 Node-API 实施状态

更新时间：2026-07-14  
结论：C++ Core 第一阶段高分辨率优化已实现：默认 bounded/960、短边 64、32 向上对齐、batch 1 流式 recognition、ORT output owning view、schema 1.1 bundle、双 profile parity 与独立进程内存门槛均在 macOS arm64 本地通过。Node-API 字段映射及 Node.js 22 本地真实模型测试也已通过。项目仍不能宣称发布验收完成，直到更新后的 Core 四平台 CI、Node 22/24 四平台预编译矩阵、npm 打包和 registry 证据完成；`tiled` 属于后续准确模式，不阻塞 bounded 第一阶段代码完成。

状态含义：

- **Done**：代码存在，并有本地实际运行证据。
- **Configured**：自动化已写好，但当前工作区未产生真实远端 run 证据。
- **Pending**：需要外部平台或制品仓动作。

## 需求验收矩阵

| `requirements.md` §19 条目 | 状态 | 当前证据或缺口 |
| --- | --- | --- |
| 四个 Tier 1 原生构建/测试 | Previous baseline Done / current Pending | [GitHub Actions run 29302144336](https://github.com/arcships/light-ocr/actions/runs/29302144336) 是 D013 前基线；当前 bounded/streaming 代码仍须重跑六个 jobs。 |
| 生产 Core 无 Python、无子进程 | Done | `light_ocr_core` 仅 C++；Python 只在 oracle/generator/report tools；Core 无 process/shell API。 |
| raw-pixel 公共 API、ownership/lifecycle 文档 | Done | `include/light_ocr/*.hpp` 与 [native-api.md](native-api.md)。 |
| detection/geometry/crop/recognition/decode 分层与测试 | Done | 独立源码模块、unit tests、stage probe 和真实模型 integration tests。 |
| PP-OCRv6 bundle 固定、哈希、许可、离线可用 | 部分 Done | `ppocrv6-small-onnx-20260714.1`、schema 1.1、原始归档、成员、dictionary、manifest 和 USTAR 已锁定；archive 为 31,334,400 bytes、SHA-256 `74e246bf…2de17`。npm tarball/integrity 与 registry 证据尚未生成。 |
| stage 与 final parity | Done（本机） | `upstream_exact` 与 `bounded_default` 均为 14/14；候选级 trace 完整；PX-0001/PX-0002 分别覆盖两 profile 中同根因的已拒绝候选 score。当前四平台证据待重跑。 |
| 首 bundle ground-truth quality report | Done（本机） | bounded 默认在 10 个锁定 fixtures 上 10/10 exact、CER `0`；IoU≥0.5 下 detection precision/recall/Hmean 均为 `1.0`。旧 exact 基线仍独立保留。 |
| 相对性能门槛 | Done（参考本机） | bounded 默认：median `0.9824867× ≤ 1.10×`；p95 `1.0139793× ≤ 1.15×`；inference median `0.9961966× ≤ 1.05×`。受控 CI worker 报告仍应保留。 |
| Sanitizer、fuzz、leak、lifecycle、malformed input | Done（当前代码） | 本机 ASan+UBSan、TSan、standalone fuzz、lifecycle 和 malformed model/tensor 已通过；最新 CI safety job 的 sanitizers、TSan 和 libFuzzer smoke 通过。D013 新路径必须进入相同 gates。 |
| 无 network/shell/cwd/locale 运行依赖 | Done（当前代码） | 本地 sterile cwd+minimal env 两次结果一致；最新 Linux job 的 network namespace disabled 测试通过。 |
| manifest、hash、licenses、SBOM、parity、benchmark | Done（当前 CI artifacts） | 最新 Tier 1 workflow 已生成并上传平台 metadata；D013 和 npm package 完成后仍需从最终 release commit 重新生成发布制品及 hashes。 |
| N-API/npm 非本 Core milestone | 源码与发布自动化 Done / package 发布 Pending | `bindings/node` 已有 raw Node-API v8 addon、CJS/ESM facade、`.d.ts`、安全 bundle loader、默认 model package 解析、四平台 native package 选择、专用 FIFO worker、双重背压、AbortSignal 和生命周期管理；`tools/npm_release.py` 与手动 `npm release` workflow 已实现六包确定性 staging、inventory/hash、Node 22/24 四平台 smoke、registry 分阶段发布和禁网复验。首次 prebuild、registry 与 provenance 证据仍待 workflow 实际完成。 |
| 高分辨率峰值内存 | Done（macOS arm64）/ Pending（其他 Tier 1） | Release 原生独立进程：2048² 空白 `318.8 MiB ≤ 384 MiB`；xfund 密集表单 116 框 `400.5 MiB ≤ 640 MiB`，同时低于 512 MiB 目标。报告锁定 detection `[1,3,960,960]` 与所有 recognition batch size 1。 |

## 本机最终验证快照

环境：macOS arm64，Apple Clang 21.0.0，CMake 4.2.1，macOS deployment target 13.3，ONNX Runtime CPU，intra/inter-op threads 均为 1。

| 验证 | 结果 |
| --- | --- |
| Release CTest | 16/16 passed；包括 unit、integration、双 profile golden/parity、quality 和两项原生 memory gate |
| 全阶段语料 | `upstream_exact` 14/14；`bounded_default` 14/14 |
| 质量基线 | bounded 10/10 exact；0/104 CER；10 TP / 0 FP / 0 FN，detection P/R/Hmean = 1.0（IoU≥0.5） |
| ASan + UBSan | 2/2 passed；Apple 平台不启用 LSan |
| TSan | 2/2 passed |
| standalone fuzz | image 100k、bundle 100k、geometry 100k、lifecycle 10，全部完成 |
| leak/RSS | 5 warmup + 10 cycles；当前复测 growth 21,413,888 bytes（2,141,388 bytes/cycle）；gate 32 MiB / 8 MiB per cycle |
| offline contract | sterile cwd/minimal locale environment passed |
| model archive | 31,334,400 bytes；SHA-256 `74e246bf075c141da51e58515c731298fdabee9fd5bd8feb7cf6c7f4f352de17` |
| Node-API v1 | Node.js 22.13.0；macOS arm64 Release/Werror 构建；CTest 3/3；bounded/exact 映射、真实 PP-OCRv6 API、snapshot/byteOffset、校验、symlink root、双重背压、abort、heartbeat、close/worker teardown 测试通过 |

性能报告（5 warmup + 30 iterations，`generated-hello-123`）：

| 指标 | Native | Python oracle | 比率 | 门槛 |
| --- | ---: | ---: | ---: | ---: |
| warm median end-to-end | 75,678 µs | 77,027 µs | 0.9824867× | ≤ 1.10× |
| warm p95 end-to-end | 79,788 µs | 78,688 µs | 1.0139793× | ≤ 1.15× |
| inference-only median | 74,125 µs | 74,408 µs | 0.9961966× | ≤ 1.05× |

这些数值是这台机器上的验收快照，不是所有硬件的绝对性能承诺。

## 发布前必须补齐

1. 在 Linux x64、Windows x64、macOS x64 重跑并保存 D013 后的 parity、quality、absolute RSS 与 lifecycle baseline；macOS arm64 已完成本地门槛。
2. 实现 npm staging/pack 脚本，把确定性 bundle 精确打入 `@arcships/light-ocr-model-ppocrv6-small`，记录 tarball SHA-256 和 registry integrity；独立 USTAR mirror 可延期。
3. 用完成 D013 和 npm packaging 的干净 release commit 重新生成四个平台的 build manifest、license inventory、SPDX SBOM、artifact hashes 和 safety reports。
4. 为 Node.js 22/24 在四个 Tier 1 平台生成 prebuild，完成 compatible-host sanitizer、worker termination、leak 与性能门槛，并保存不可变测试证据。
5. 根 `LICENSE`/`NOTICE` 已按 Apache-2.0 提交；仍需完成六个 `@arcships` packages 的 sterile tarball install、默认 `createEngine()` 和禁网运行验证后再发布。

在以上事项完成前，应称为“C++ Core 实现完成、发布验收待补”，而不是“整个 milestone 已发布完成”。
