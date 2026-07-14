# npm 0.2.0 发布候选记录

状态：准备中

目标版本：`0.2.0`

协议：Apache-2.0

## 面向使用者的变化

- 新增可选 `tiled-v1` detection strategy，面向 2048 像素级小字、密集文本和跨 tile 边界内容；bounded/960 仍是默认策略。
- Node.js 新增 `recognizeEncoded(Uint8Array)`，可直接识别内存中的 JPEG 和 PNG；不会读取文件路径，也不会改变 C++ Core 的 raw-pixel API。
- npm 仍使用一个 facade、一个随包安装的 PP-OCRv6 Small 模型包和当前平台的一个 native package；安装或首次运行不额外下载模型。
- CJS、ESM 和 TypeScript 类型保持同步，支持 Node.js 22/24，以及 macOS arm64/x64、Linux x64 glibc、Windows x64。

## 兼容性与边界

- `createEngine()` 默认仍使用 bounded/960；tiled 必须通过 `detection: { strategy: "tiled" }` 显式选择。
- `recognize()` 的 raw pixel ownership、snapshot、queue、AbortSignal 和 lifecycle 契约不变。
- `recognizeEncoded()` 当前只支持 JPEG/PNG，不支持 WebP、GIF、PDF、文件路径或自动 EXIF orientation。
- 模型 bundle 更新为 `ppocrv6-small-onnx-20260714.2`，包含 normalized schema `1.2` 和版本化 `tiled-v1` runtime contract。

## 发布门槛

- [x] tiled-v1 的八张 2048² ground truth、重复行消除、reading order 与独立 oracle 已通过。
- [x] 合并后的 encoded JPEG/PNG Release Node integration test 已在 macOS arm64 本机通过。
- [x] 普通 CI 与 npm release preflight 不自动运行 benchmark。
- [x] 显式四平台 tiled qualification 已生成、review 并提交 accepted baseline。
- [ ] 合并后 Core CI 与无 benchmark npm release preflight 全绿。
- [ ] 六个 `0.2.0` tarballs 已发布到 npm `next`，完成 registry/禁网复验并提升到 `latest`。
- [ ] workflow、provenance、tarball hashes 与 registry integrity 已回填本记录。

发布完成后，本文件会从候选记录更新为不可变发布证据；`0.1.0` 的记录与制品保持不变。

## Tiled qualification 证据

- [qualification run 29336329115](https://github.com/arcships/light-ocr/actions/runs/29336329115) 的 Linux x64、Windows x64、macOS arm64、macOS x64 四个采样 jobs 全部成功，Core、Node 22、Node 24 共 36 个 baseline entries 和原始报告 artifacts 均已保存。
- run 的旧 collect job 因跨独立进程执行 `Core peak + 64 MiB` 相对门槛而失败；review 后确认 Node 进程本身的 runtime 基线和 12 MiB fixture snapshot 不能由 Core 进程峰值表达。修正后的 collector 复用原始 artifacts，不重新采样，并保留 Node/Core latency、peak delta 为 non-blocking observations。
- accepted baseline 固定 Core `<= 1 GiB`、Node `<= 1088 MiB`、单调用 `< 120 s` 和质量/稳定性 hard gates；后续每个实现相对自身受审 baseline 的 median、p95 或 absolute peak 增长超过 `15%` 时失败。
- 四平台实测最大值：Linux x64 Core/Node `639.7/715.6 MiB`，Windows x64 `616.1/667.5 MiB`，macOS arm64 `667.4/733.6 MiB`，macOS x64 `623.1/672.8 MiB`。
- baseline 来源 commit：`21d812dcb60e642036c285647571d900b57cc3a6`；`contracts/tiled-platform-baselines.json` SHA-256：`9b40d646435cd0e6d0ee33c66c79ff4e30182ece71749cc4f728f1219b396217`。
