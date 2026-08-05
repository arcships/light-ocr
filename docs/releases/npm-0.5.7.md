# npm 0.5.7 发布记录

[English version](npm-0.5.7.en.md)

状态：发布前验证已完成。候选版本尚未写入 npm registry；当前稳定
Small/runtime/native 闭包仍为 `0.5.6`。六平台演练与 manifest 审计全绿，
可进入正式发布。

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
- registry 发布与回装验证：待执行
- 稳定 dist-tag 晋升：待执行
- GitHub Release：待创建

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

## 待发布版本闭包

| 成熟度 | 包 | 候选版本 | 目标标签 |
| --- | --- | ---: | --- |
| stable | `@arcships/light-ocr` | `0.5.7` | `next`，验证后晋升 `latest` |
| stable | `@arcships/light-ocr-runtime` | `0.1.7` | `next`，验证后晋升 `latest` |
| stable | 六个平台 native | `0.5.7` | `next`，验证后晋升 `latest` |
| compatibility | `@arcships/light-ocr-document` | `0.1.3` | `next` |
| preview | `@arcships/light-ocr-tiny` | `0.1.6` | `next` |
| preview | `@arcships/light-ocr-medium` | `0.1.6` | `next` |

模型包不重发新版本：Small 继续使用 `0.3.4`，Tiny/Medium 继续使用
`0.1.0`。发布流水线重建或取得这些不可变模型 tarball 用于完整离线安装
验证，并只在 registry 中同版本 integrity 完全一致时复用。

## 六平台 native 候选制品

下表来自发布演练的 `release-manifest.json`。增量以已发布 `0.5.6`
tarball 为基线；用户只安装当前平台对应的一个 native 包。

| 平台包 | 压缩 bytes | 相对 0.5.6 | 解包 bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| `darwin-arm64` | 22,889,540 | -2 | 56,356,978 | `55f5539456e8da241d8101c7ca5f015b9ab130d77758b5a99552f3b5467c9554` |
| `darwin-x64` | 24,853,682 | +1 | 62,109,821 | `18975175b52da136d00e740f3a55ba4aab582732a80390b6c21f5b7053f0e51e` |
| `linux-arm64-gnu` | 20,453,508 | +23 | 40,261,084 | `ffb45dbff2f585010dade1978465b7e5b521c4da01db4dbc955cb20b27d889b7` |
| `linux-x64-gnu` | 27,033,573 | -18 | 59,390,897 | `a3bb20608d785a054b34c90a579f695a72648dae5db91e321167648b97c2e2c4` |
| `win32-arm64` | 16,661,617 | -2,136 | 30,977,284 | `7bbb501cfef7909ec319218c57fd5ed732aa619260ddbf5c39649c7005e3dc3c` |
| `win32-x64` | 30,386,121 | -1,022 | 63,065,652 | `84804a4fa8db6657361d59a2645c6140b00e48de2063a5dab182156ee4fb2d0e` |

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
- [ ] 以 `publish_to_registry=true` 发布不可变候选到 `next`
- [ ] 从 npm registry 回装并核对 integrity 与禁网运行
- [ ] 将 stable Small/runtime/native 闭包晋升到 `latest`
- [ ] 创建 `v0.5.7` GitHub Release

## 发布与回滚顺序

1. 只从通过主干验证的 `main` 运行 `npm release` dry-run；下载
   `light-ocr-npm-0.5.7`，审计 manifest、包数、版本、bytes、SHA-256 与
   npm integrity。
2. 合并本记录后，以 `publish_to_registry=true` 重跑同一六平台流程。
   新 package identity 只发布到 `next`，随后从 registry 回装并执行离线
   图片/PDF smoke。
3. 仅在发布 run 全绿后，使用该 run 的不可变 artifact 将 Small
   `0.5.7`、runtime `0.1.7` 与六个 native `0.5.7` 晋升到 `latest`。
   Document、Tiny 与 Medium 不属于 stable promotion，继续保持 `next`。
4. 如晋升后发现阻断问题，不覆盖或删除已发布版本；使用已归档的
   `0.5.6` 发布 artifact（run `30599969242`）将 stable 标签回退到
   Small/native `0.5.6` 与 runtime `0.1.6`。

## GitHub Release 草案

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
- release workflow: pending
- stable promotion: pending
