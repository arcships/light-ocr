# npm 0.2.0 发布记录

发布日期：2026-07-14<br>
发布 commit：`05e6e73069b29332d4cc7759d58ccd99767f0a40`

版本：`0.2.0`

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
- [x] 合并后 Core CI 与无 benchmark npm release preflight 全绿。
- [x] 六个 `0.2.0` tarballs 已发布到 npm `next`，完成 registry/禁网复验并提升到 `latest`。
- [x] workflow、provenance、tarball hashes 与 registry integrity 已回填本记录。

`0.1.0` 的记录、dist 和制品保持不变；0.2.0 的六个 package version 同样不可覆写。

## Tiled qualification 证据

- [qualification run 29336329115](https://github.com/arcships/light-ocr/actions/runs/29336329115) 的 Linux x64、Windows x64、macOS arm64、macOS x64 四个采样 jobs 全部成功，Core、Node 22、Node 24 共 36 个 baseline entries 和原始报告 artifacts 均已保存。
- run 的旧 collect job 因跨独立进程执行 `Core peak + 64 MiB` 相对门槛而失败；review 后确认 Node 进程本身的 runtime 基线和 12 MiB fixture snapshot 不能由 Core 进程峰值表达。修正后的 collector 复用原始 artifacts，不重新采样，并保留 Node/Core latency、peak delta 为 non-blocking observations。
- accepted baseline 固定 Core `<= 1 GiB`、Node `<= 1088 MiB`、单调用 `< 120 s` 和质量/稳定性 hard gates；后续每个实现相对自身受审 baseline 的 median、p95 或 absolute peak 增长超过 `15%` 时失败。
- 四平台实测最大值：Linux x64 Core/Node `639.7/715.6 MiB`，Windows x64 `616.1/667.5 MiB`，macOS arm64 `667.4/733.6 MiB`，macOS x64 `623.1/672.8 MiB`。
- baseline 来源 commit：`21d812dcb60e642036c285647571d900b57cc3a6`；`contracts/tiled-platform-baselines.json` SHA-256：`9b40d646435cd0e6d0ee33c66c79ff4e30182ece71749cc4f728f1219b396217`。

## 自动化与 registry 证据

- [Core CI run 29339087671](https://github.com/arcships/light-ocr/actions/runs/29339087671)：四个平台、oracle、ASan/UBSan、TSan 与 libFuzzer smoke 全部成功。
- [无发布预检 run 29339091349](https://github.com/arcships/light-ocr/actions/runs/29339091349)：四平台构建、Node 22/24 八组 package tests、CJS/ESM、TypeScript、真实 bounded/tiled OCR、一次性 registry 和禁网复验全部成功；未运行 benchmark。
- [npm release run 29340467784](https://github.com/arcships/light-ocr/actions/runs/29340467784)：使用 npm 11 和 provenance 成功发布六包到 `next`，并保存确定性 release artifact；最后的即时 dist-tag 查询受 npm metadata 短暂缓存影响而失败，但 facade 的更新随后可见。
- [npm promotion run 29342178842](https://github.com/arcships/light-ocr/actions/runs/29342178842)：复用上述 release artifact，逐包核对已发布 integrity，按 model/native 依赖优先、facade 最后的顺序把六包提升到 `latest`。promotion verifier 已改为轮询 registry convergence。
- 最终公开查询确认六包的 `next` 与 `latest` 均指向 `0.2.0`。

## 不可变制品

以下数据来自 release run 保存的 `release-manifest.json`；manifest SHA-256 为 `05255cafe8efcf0d063710e36ca863c5cf642b47ec1f40ae6037ad363455040c`，registry 的 `dist.integrity` 已逐包复核一致。

| Package | Tarball bytes | Unpacked bytes | SHA-256 |
| --- | ---: | ---: | --- |
| `@arcships/light-ocr` | 8,862 | 27,922 | `ae7a5be432f6cd2ef958d51d03c20e1dff77367d727374b156d74394fef75cda` |
| `@arcships/light-ocr-model-ppocrv6-small` | 26,091,308 | 31,333,440 | `8034e02384f74781871140c2a8dfaf8ccc3f07b8672000dd2d77dfaed7d49730` |
| `@arcships/light-ocr-darwin-arm64` | 11,963,182 | 39,759,193 | `6891bfd61a1fd08dc9d7e25e1a405ee7b569ff12f03135e1d4ddb4609c706152` |
| `@arcships/light-ocr-darwin-x64` | 13,888,477 | 45,767,579 | `9ef089d624671a00189cb696c6d1bc61e604145978a19b0b812faa31f011a877` |
| `@arcships/light-ocr-linux-x64-gnu` | 11,775,067 | 32,291,209 | `6ef4e9a937dd66280b9fca200cd907a83daaae2b8ae88afb70dd7c8f4ee39ee4` |
| `@arcships/light-ocr-win32-x64` | 6,212,642 | 16,497,737 | `4f6e92d0aefe2fffa171128f87f46d076e5cc4b77e0d4cc5b63b2ad8d3db3b2f` |

Registry integrity：

```text
@arcships/light-ocr
sha512-Lbzp+kVw+ME4F+vgIwOtFe4iCkSf+IA2DRIsgvxDZwGZIo01A9l92sqqvTuJysW0ctmF3bn3edlvZ491mtK8lQ==

@arcships/light-ocr-model-ppocrv6-small
sha512-her+a6JVkOezdYNgeAUdPSAQUFrJflvrOwK7Au5a5QKUFp0bGZAZm/la2rNYCZlImHbl5eK6r2/+EqwKzU9C+A==

@arcships/light-ocr-darwin-arm64
sha512-gadTpN2x76V7aFsBpyJKZiSM+7vDfmDdhRl4ErAZ2eF5L2v2WEJzkkALzXaAoemWRDIgfAi2xH5Irc4y2CBScQ==

@arcships/light-ocr-darwin-x64
sha512-DjDYwHZKYYSi+14fV0wK4mDK3T2c2YTE3z2Jb7r7HWPo7G5qVoKct+Gy8WMlBcJto62vuvEUxHGvAMhuTDL5ZQ==

@arcships/light-ocr-linux-x64-gnu
sha512-GKAR6q/DNhcb78FomzAaCzHPDKM3kKRysIpNw9kgOlMnpCatExWHZNkDNniG/D1oSDV1CY/WBYAhLZtCcVd5OA==

@arcships/light-ocr-win32-x64
sha512-06mQUMPP5Rmf34THVR2CWHN3Y3R8T9EbFwekbcMdlZzn+W3gBUJ9SwKW6WgZzsehsFJ/yygtXsuo0kE3ctkkjA==
```
