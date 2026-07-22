# npm 0.3.2 发布记录

发布日期：2026-07-22<br>
发布 commit：`00212277cc7f333893da0c8464b28ae78182aad0`

版本：`0.3.2`

协议：Apache-2.0

## 面向使用者的变化

- 新增 `light-ocr` CLI，提供默认 `recognize`、detection-only `detect` 与诊断 `info` 入口。
- 新增 `schemaVersion: 1` 的 `DocumentResult` envelope、稳定 line/detection ID、ROI 与 JPEG EXIF orientation 修正。
- 新增仓库内 Agent Skill，覆盖小字截图、表单字段、区域识别、置信度与错误恢复工作流。
- 新增 Linux arm64 glibc 与 Windows arm64 CPU native packages；release set 从六包扩展为一个 facade、一个 model 和六个 native packages，共八包。
- Windows native addon 新增 delay-load hook，可在 Electron 等重命名 Node-API host 中加载。

## 发布与验证证据

- [npm release run 29911026397](https://github.com/arcships/light-ocr/actions/runs/29911026397)：从发布 commit 构建并验证六个平台、Node.js 22/24、临时 registry、禁网运行与八个确定性 tarball；随后按依赖优先、facade 最后发布到 npm `next`。
- [GitHub Release v0.3.2](https://github.com/arcships/light-ocr/releases/tag/v0.3.2)：绑定同一发布 commit 的公开 release。
- [npm promotion run 29923513944](https://github.com/arcships/light-ocr/actions/runs/29923513944)：复用上述 release run 的原始 artifact，逐包核对公开 registry integrity，再按依赖优先、facade 最后提升到 `latest`；未重建或重发 tarball。
- 最终公开查询确认 facade、model 与六个 native packages 的 `next` 和 `latest` 均指向 `0.3.2`。

## 已知制品缺陷

`0.3.2` 的公开 facade tarball 未包含源码中的 `bin` 字段与 `bin/light-ocr.cjs`，因此 Node API 正常，但全新 npm 安装不会生成 `light-ocr` 命令。npm 已发布版本不可覆盖；该缺陷由补丁版本 `0.3.3` 修复，`latest` 随后移至 `0.3.3`。本记录保留 `0.3.2` 的原始哈希，不把后续制品冒充为同版本重发。

## 不可变制品

以下数据来自 release run 保存的 `release-manifest.json`；manifest SHA-256 为 `f8b5cb50fb305f0f333459e7cb31755ec44010a9092d893d0a9034ce403fce05`，`gitRevision` 为发布 commit，npm 版本为 `11.0.0`。promotion workflow 已逐包复核 manifest 中的 `dist.integrity` 与公开 registry 一致。

| Package | Tarball bytes | Unpacked bytes | SHA-256 |
| --- | ---: | ---: | --- |
| `@arcships/light-ocr` | 15,774 | 58,222 | `22e378522a5dc89ea1b4d9855dc39e7c4587106c8c109354021b2fed314ec56b` |
| `@arcships/light-ocr-model-ppocrv6-small` | 52,529,118 | 73,594,252 | `4c1681504dd162f4b343f1590970b2730e2fde06f7f7080c5e8a395fc1cd8b66` |
| `@arcships/light-ocr-darwin-arm64` | 12,077,655 | 40,007,690 | `949a5c700d39e1f71f44e9eb63450e4a0c08f44e22e051db8ac81f4ef8bcc7e0` |
| `@arcships/light-ocr-darwin-x64` | 14,008,925 | 46,045,885 | `7930ac3f41e1f3ed8676a91a68f2495f6cfc512c8d16e312393d82a115793093` |
| `@arcships/light-ocr-linux-arm64-gnu` | 9,847,553 | 25,188,559 | `7e341fb03498b39fb311b42b2983ea8b43bb2b1895df63e44e7c0d78ff40a57c` |
| `@arcships/light-ocr-linux-x64-gnu` | 16,318,723 | 44,177,523 | `d60c82b7491ffd9181aa631911a65ff815a55e304d5d0ac60f66f01470a10004` |
| `@arcships/light-ocr-win32-arm64` | 5,782,803 | 15,586,199 | `592faf431e7a540cfcfd17430837dfa533402a20a40d883a8e463c501cf2564e` |
| `@arcships/light-ocr-win32-x64` | 19,306,970 | 47,101,887 | `11f846c38052108964666515b51df33701618fa115d0acfae327eaa5cf83e4a2` |
