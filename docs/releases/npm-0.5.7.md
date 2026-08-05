# npm 0.5.7 发布记录

[English version](npm-0.5.7.en.md)

状态：已于 2026-08-05 发布。稳定 Small/runtime/native 闭包同时位于
`latest` 与 `next`；Document compatibility facade 和 Tiny/Medium preview
facade 保持在 `next`。

发布身份：

- 实现与发布门补强：
  [PR #59](https://github.com/arcships/light-ocr/pull/59)
- 合并后的 `main` 提交：
  [`1a26d53`](https://github.com/arcships/light-ocr/commit/1a26d53b97c4260bce2af6badeebd9cb3fe48484)
- 最终 `main` 验证：
  [Core 30986015180](https://github.com/arcships/light-ocr/actions/runs/30986015180)
  与
  [Native WebGPU 30986015218](https://github.com/arcships/light-ocr/actions/runs/30986015218)
- 六平台发布演练：
  [30986812237](https://github.com/arcships/light-ocr/actions/runs/30986812237)
  （`publish_to_registry=false`）
- registry 发布与回装验证：
  [30988312627](https://github.com/arcships/light-ocr/actions/runs/30988312627)
- 稳定 dist-tag 晋升：
  [30989501173](https://github.com/arcships/light-ocr/actions/runs/30989501173)
- GitHub Release：
  [`v0.5.7`](https://github.com/arcships/light-ocr/releases/tag/v0.5.7)

## 用户可见变化

- 修复 Electron、notarization 或其他下游 macOS 打包流程重签原生载荷后，
  runtime 因文件 size/SHA-256 改变而拒绝加载的问题。`light_ocr_node.node`
  与 ONNX Runtime dylib 现在可以随宿主应用使用同一 Developer ID 重签。
- descriptor 的 bytes/SHA-256 精确匹配仍是首要完整性门。只有 macOS 上
  该门不匹配时，loader 才检查 Mach-O magic、执行
  `codesign --verify --strict`，并要求制品 TeamIdentifier 与宿主进程相同；
  双方均为 ad-hoc 签名时也允许加载。
- 不同 TeamIdentifier、未签名篡改、非 Mach-O 文件、畸形 descriptor，
  以及 Linux/Windows 的任何 descriptor 不匹配仍返回
  `package_load_failed`。native inventory 与加载后的 ABI contract 不变。
- Electron 应用无需配置、环境变量、postinstall、证书 keychain 或新增依赖。
  `codesign` 只用于验证。由于 ad-hoc 签名可由任何人复现，该分支明确限制
  为宿主与制品双方都采用 ad-hoc 的 macOS 场景。
- 没有 public API 或 schema 破坏；图片、PDF、多页图片、CLI 与 provider
  行为保持不变。

## 已发布版本闭包

| 成熟度 | 包 | 已发布版本 | 最终标签 |
| --- | --- | ---: | --- |
| stable | `@arcships/light-ocr` | `0.5.7` | `latest`、`next` |
| stable | `@arcships/light-ocr-runtime` | `0.1.7` | `latest`、`next` |
| stable | 六个平台 native | `0.5.7` | `latest`、`next` |
| compatibility | `@arcships/light-ocr-document` | `0.1.3` | `next` |
| preview | `@arcships/light-ocr-tiny` | `0.1.6` | `next` |
| preview | `@arcships/light-ocr-medium` | `0.1.6` | `next` |

模型包没有重发新版本：Small 继续使用 `0.3.4`，Tiny/Medium 继续使用
`0.1.0`。发布流水线重建或取得这些不可变模型 tarball 完成完整离线安装
验证，并在确认 registry 同版本 integrity 完全一致后复用。

## 六平台 native 制品

下表来自正式发布 run 的 `release-manifest.json`，且 npm registry integrity
已逐包核对一致。增量以已发布 `0.5.6` tarball 为基线；用户只安装当前
平台对应的一个 native 包。

| 平台包 | 压缩 bytes | 相对 0.5.6 | 解包 bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| `darwin-arm64` | 22,889,533 | -9 | 56,356,978 | `d8d0b30f1c30c989d8392d438be3b3e06c0b0f3c86a19bb125dfa6732857dad6` |
| `darwin-x64` | 24,853,690 | +9 | 62,109,821 | `4b69cbe6aebf44dd052f37fd7173b386b446ebff040e003278d08a9a6d9a77d6` |
| `linux-arm64-gnu` | 20,453,503 | +18 | 40,261,084 | `81d2f306c247aa21ef18ff881e33df419de8b27c71adf7acf46c50838dd29d7f` |
| `linux-x64-gnu` | 27,033,585 | -6 | 59,390,897 | `6f83bdfbce7500e56bb0c31001a55143e9874257dafe679dcaad406980b72750` |
| `win32-arm64` | 16,661,640 | -2,113 | 30,977,284 | `8c702941470a549ffda07cf0ba9e224824c39d5debf6046446018520c1602f01` |
| `win32-x64` | 30,386,117 | -1,026 | 63,065,652 | `aa2d792a2e476073b39c9d3fc8cdf225695c06b534ce85e640db10c664f25baa` |

## 签名策略验证

- PR CI 在 GitHub macOS runner 的默认 Node 宿主上执行真实 `codesign`
  集成测试，覆盖原始制品、未签名篡改、非 Mach-O、异签名和 descriptor
  畸形记录。
- release workflow 复制 setup-node 的 Node 可执行文件并 ad-hoc 重签，
  再次运行相同测试，确定性覆盖“宿主与制品均为 ad-hoc”正向路径。
- Developer ID signed host 的手工验证使用 TeamIdentifier `3AA79YWT4C`：
  原始制品通过；未签名篡改拒绝；由 ad-hoc 身份重签的制品因签名主体
  不同而拒绝。若 runner keychain 没有匹配 Developer ID，测试会明确跳过
  同 Team 正向路径，不触发 keychain UI 或伪造成功证据。

## 发布门

- [x] PR 的 Linux 原生构建与 workspace Node 测试
- [x] 合并后的六平台 Core、sanitizer、fuzzer 与 oracle
- [x] Linux/Windows Native WebGPU contract
- [x] 默认 macOS Node 宿主的真实签名策略测试
- [x] ad-hoc 重签 Node 宿主的双方 ad-hoc 正向测试
- [x] Developer ID signed host 的原始/篡改/异签手工负向验证
- [x] 畸形 bytes/SHA-256 descriptor 在 fallback 前 fail closed
- [x] 11 个新 package identity 的 npm registry 空位检查
- [x] 六个平台从锁定源码重建 native 与 PDFium addon
- [x] 六个平台离线、禁用安装脚本安装完整闭包
- [x] 六个平台真实图片 OCR 与非嵌入中文字体 PDF OCR
- [x] 候选 tarball manifest、bytes、SHA-256 与 npm integrity 审计
- [x] 以 `publish_to_registry=true` 发布不可变候选到 `next`
- [x] 从 npm registry 回装并核对 integrity 与禁网运行
- [x] 将 stable Small/runtime/native 闭包晋升到 `latest`
- [x] 创建 `v0.5.7` GitHub Release

## 实际发布结果与回滚

1. [演练 run 30986812237](https://github.com/arcships/light-ocr/actions/runs/30986812237)
   的 14 个 jobs 全绿；13 包 manifest 的来源 SHA、版本、唯一性、bytes、
   SHA-256、shasum 与 npm integrity 审计通过，且没有写入 registry。
2. [发布 run 30988312627](https://github.com/arcships/light-ocr/actions/runs/30988312627)
   重复六平台构建与离线 smoke，将新 package identity 发布到 `next`，
   再从 registry 回装并验证 integrity、图片/PDF OCR 与禁网运行。
3. [晋升 run 30989501173](https://github.com/arcships/light-ocr/actions/runs/30989501173)
   只把 Small `0.5.7`、runtime `0.1.7` 与六个 native `0.5.7` 晋升到
   `latest`。Document、Tiny 与 Medium 继续保持 `next`。
4. [`v0.5.7` GitHub Release](https://github.com/arcships/light-ocr/releases/tag/v0.5.7)
   绑定正式发布来源提交 `bff2243`，记录公开发布。

如需回滚，不覆盖或删除已发布版本；使用已归档的 `0.5.6` 发布 artifact
（run `30599969242`）将 stable 标签恢复到 Small/native `0.5.6` 与 runtime
`0.1.6`。

## GitHub Release

Release name：

`同印随舟，重签安渡 · Support re-signed macOS artifacts in 0.5.7`

Highlights：

- Allow downstream macOS packaging pipelines to re-sign the native addon and
  ONNX Runtime dylib with the same Developer ID as the host application.
- Keep descriptor bytes/SHA-256 as the primary integrity gate and use strict
  code-signature equivalence only for a mismatched macOS Mach-O.
- Accept identical TeamIdentifier signatures or the explicitly documented
  both-ad-hoc development case; reject different identities, unsigned
  mutations, non-Mach-O payloads, and malformed descriptors.
- Require no Electron configuration, postinstall hook, keychain access, or new
  dependency, while preserving every public API and non-macOS behavior.

Versions：

- `@arcships/light-ocr@0.5.7`
- `@arcships/light-ocr-runtime@0.1.7`
- six native platform packages at `0.5.7`
- Document compatibility facade `0.1.3` under `next`
- Tiny/Medium preview facades `0.1.6` under `next`
- unchanged Small `0.3.4` and Tiny/Medium `0.1.0` model packages

Verification：

- implementation and PR checks:
  <https://github.com/arcships/light-ocr/pull/59>
- final Core:
  <https://github.com/arcships/light-ocr/actions/runs/30986015180>
- final Native WebGPU:
  <https://github.com/arcships/light-ocr/actions/runs/30986015218>
- pre-release dry-run:
  <https://github.com/arcships/light-ocr/actions/runs/30986812237>
- release workflow:
  <https://github.com/arcships/light-ocr/actions/runs/30988312627>
- stable promotion:
  <https://github.com/arcships/light-ocr/actions/runs/30989501173>
- GitHub Release:
  <https://github.com/arcships/light-ocr/releases/tag/v0.5.7>
