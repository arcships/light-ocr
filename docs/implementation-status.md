# C++ Core 与 Node-API 实施状态

更新时间：2026-07-14  
结论：已发布的 `@arcships/light-ocr@0.1.0` 及其 bounded/960 行为不变。当前源码正在准备 `0.2.0`：`tiled-v1`、schema 1.2 bundle、八张独立 ground truth、Python oracle、确定性/质量门禁，以及 Node.js 内存 JPEG/PNG 输入均已实现；无 benchmark 的四平台发布预检曾在 tiled commit 上通过，新合并的 encoded-input 改动已通过本机真实 Node 测试。首次四平台受审 peak/latency baseline、合并后 release preflight 和 registry release evidence 仍待产生。

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
| PP-OCRv6 bundle 固定、哈希、许可、离线可用 | Done（0.1.0）/ candidate（0.2.0） | 已发布 `.1`/schema 1.1 证据保持不变；当前 `.2` candidate 使用相同 ONNX bytes，新增 schema 1.2、`tiled-v1` contract、新 manifest/config/archive hash，并把 minimum Core 提升到 0.2.0。 |
| stage 与 final parity | Done | `upstream_exact` 与 `bounded_default` 均为 14/14；候选级 trace 完整；release commit 的 oracle 与四平台 jobs 全绿。 |
| 首 bundle ground-truth quality report | Done（本机） | bounded 默认在 10 个锁定 fixtures 上 10/10 exact、CER `0`；IoU≥0.5 下 detection precision/recall/Hmean 均为 `1.0`。旧 exact 基线仍独立保留。 |
| 相对性能门槛 | Done（参考本机） | bounded 默认：median `0.9824867× ≤ 1.10×`；p95 `1.0139793× ≤ 1.15×`；inference median `0.9961966× ≤ 1.05×`。受控 CI worker 报告仍应保留。 |
| Sanitizer、fuzz、leak、lifecycle、malformed input | Done | 本机 ASan+UBSan、TSan、standalone fuzz、lifecycle 和 malformed model/tensor 已通过；release Core safety job 的 sanitizers、TSan 和 libFuzzer smoke 全绿。 |
| 无 network/shell/cwd/locale 运行依赖 | Done | sterile cwd/minimal env 与 Linux network namespace disabled 测试通过；npm release 另完成已安装 package 的禁网运行。 |
| manifest、hash、licenses、SBOM、parity、benchmark | Done | Release commit 已重新生成并保存四平台 metadata、六个 npm tarballs 的 hashes/integrity、parity、quality 与 benchmark 证据。 |
| N-API/npm 非本 Core milestone | Done / `0.1.0` published | raw Node-API v8、CJS/ESM、`.d.ts`、内置模型解析、四平台 prebuild、双重背压、AbortSignal 与生命周期均已完成；[npm release run 29312486301](https://github.com/arcships/light-ocr/actions/runs/29312486301) 的 Node 22/24 八组测试、registry 分阶段发布和禁网复验全绿。 |
| Node.js JPEG/PNG 内存输入 | Done（源码）/ `0.2.0` candidate | `recognizeEncoded(Uint8Array)` 在 engine worker 上使用固定 stb revision 解码，保持 Core raw-pixel 边界；格式、尺寸、pixels、临时内存、queue/snapshot budget、AbortSignal 与 `timingUs.decode` 均有测试。合并后本机 Release Node integration test 已通过。 |
| 高分辨率峰值内存 | Done | Release 原生独立进程本机参考：2048² 空白 `318.8 MiB ≤ 384 MiB`；xfund 密集表单 116 框 `400.5 MiB ≤ 640 MiB`。四平台 release jobs 的真实模型与 RSS gates 均通过。 |
| Tiled 高分辨率准确模式 | Release candidate / not published | 1280 tile、2048→4-pass row-major、全局 candidate ceiling、IoU/IOS greedy merge、原图 recognition、C++/Node contract、8-fixture/196-line corpus、独立 oracle、Core/Node memory/latency 与 package smoke 已实现；四平台 accepted baseline 和 0.2.0 发布仍是硬缺口。 |

## 本机最终验证快照

环境：macOS arm64，Apple Clang 21.0.0，CMake 4.2.1，macOS deployment target 13.3，ONNX Runtime CPU，intra/inter-op threads 均为 1。

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
| model archive | 已发布 `.1`：31,334,400 bytes / `74e246bf…de17`；tiled candidate `.2`：31,334,400 bytes / `e543b93b…712f` |
| Node-API v1 | Node.js 22.13.0；macOS arm64 Release/Werror 构建；CTest 3/3；bounded/exact 映射、真实 PP-OCRv6 API、snapshot/byteOffset、校验、symlink root、双重背压、abort、heartbeat、close/worker teardown 测试通过 |
| Tiled candidate | 八张 2048² locked fixtures 共 196 行：196 TP / 0 FP / 0 FN、CER 0、duplicate line 0；独立 oracle 与原生 pass tensor、candidate source、suppression、representative、crop、decode 和 final order 对齐；side override、tile ceiling、global candidate ceiling 均返回稳定错误 |
| Tiled candidate（本机探索值） | macOS arm64 四 tile 交点：Core median 2.28 s / peak 683,147,264 bytes，Node 22 median 2.29 s / peak 742,129,664 bytes；Core-vs-oracle 与 Node-vs-Core 均通过，尚不是四平台 accepted baseline |

性能报告（5 warmup + 30 iterations，`generated-hello-123`）：

| 指标 | Native | Python oracle | 比率 | 门槛 |
| --- | ---: | ---: | ---: | ---: |
| warm median end-to-end | 75,678 µs | 77,027 µs | 0.9824867× | ≤ 1.10× |
| warm p95 end-to-end | 79,788 µs | 78,688 µs | 1.0139793× | ≤ 1.15× |
| inference-only median | 74,125 µs | 74,408 µs | 0.9961966× | ≤ 1.05× |

这些数值是这台机器上的验收快照，不是所有硬件的绝对性能承诺。

## 首发结论与后续范围

`0.1.0` 的四平台 Core、Node.js 22/24 prebuild、六包确定性制品、public registry、provenance、默认 `createEngine()` 与禁网运行证据已经完成，详见 [npm 0.1.0 发布记录](releases/npm-0.1.0.md)。

当前下一步是显式运行一次独立 `tiled-qualification`，产生并 review 首次四平台 baseline；这是首次公开 tiled contract 所需的一次性资格审查。随后在合并后的源码上重新运行不含 benchmark 的 `publish_to_registry=false` release preflight，确认 encoded input、Node 22/24、六包、本地 registry 与禁网 OCR。普通 push、PR 和 release workflow 均不运行 benchmark。完成定义全绿前不发布 0.2.0，也不改变 0.1.0 的公开结论。
