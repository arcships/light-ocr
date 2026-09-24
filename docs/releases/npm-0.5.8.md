# npm 0.5.8 发布记录

[English version](npm-0.5.8.en.md)

状态：已于 2026-09-24 发布。稳定 Small/runtime/native 闭包同时位于
`latest` 与 `next`；Document compatibility facade 和 Tiny/Medium preview
facade 保持在 `next`。

发布身份：

- musl 平台与 engines 放宽实现：
  [PR #63](https://github.com/arcships/light-ocr/pull/63)
  （含两轮独立 review 修复）
- musl ONNX Runtime 产物 release：
  [`musl-runtime-1.22.0`](https://github.com/arcships/light-ocr/releases/tag/musl-runtime-1.22.0)
- 版本同步提交：
  [`947ee35`](https://github.com/arcships/light-ocr/commit/947ee3518b521a1cf76ba5169ebf4e8ef2e1ee9d0)
- 八平台发布演练：
  [36012260612](https://github.com/arcships/light-ocr/actions/runs/36012260612)
  （`publish_to_registry=false`）
- registry 发布与回装验证：
  [36013862673](https://github.com/arcships/light-ocr/actions/runs/36013862673)
- 稳定 dist-tag 晋升：
  [36018294655](https://github.com/arcships/light-ocr/actions/runs/36018294655)
- GitHub Release：[`v0.5.8`](https://github.com/arcships/light-ocr/releases/tag/v0.5.8)

## 用户可见变化

- 新增 musl（Alpine）Linux 的 CPU-only 预编译平台包
  `@arcships/light-ocr-linux-x64-musl` 与
  `@arcships/light-ocr-linux-arm64-musl`，平台包总数由六个扩展到八个。
  Alpine 宿主 `npm install @arcships/light-ocr` 直接得到可用的引擎，
  不再需要切换到 glibc 基础镜像。
- 所有发布包的 `engines.node` 从枚举式 `^22.0.0 || ^24.0.0` 放宽为下限式
  `>=22.0.0`。addon 使用 Node-API 稳定 ABI，新 Node 大版本（含 26）无需
  库发版即可加载同一份预编译产物；同时解除 Yarn 在每个新 Current 版本
  发布当天的硬失败。Tier 1 支持仍为 22/24 LTS，由文档 tier 矩阵与 CI
  smoke 表达，与 engines 安装下限分离。
- `@arcships/light-ocr-runtime` 导出 `platformIdentity()`（含 TypeScript
  声明），facade 与工具通过同一 glibc/musl 感知的平台身份解析 native
  平台包；PDF 渲染器的平台解析同样迁移到该身份。
- 没有 public API 或 schema 破坏；glibc 平台的图片、PDF、CLI 与 provider
  行为保持不变。

## musl 供应链

- onnxruntime 1.22.0 没有官方 musl 预编译产物。本项目在 `alpine:3.22`
  容器内从 v1.22.0 源码构建 CPU-only shared library（唯一 patch 为
  `stacktrace.cc` 的 `execinfo.h` include 加 `NDEBUG` guard，Release 下
  `backtrace()` 调用体本就编译为空），按 NuGet 目录形状打包，随包附带
  MIT LICENSE 与 ThirdPartyNotices.txt。
- 两个 musl 归档由 `models/deps.lock.json` 以 URL + bytes + SHA-256 锁定，
  与 glibc NuGet 包同一供应链契约；
  `.github/workflows/onnxruntime-musl.yml`（workflow_dispatch）提供按需
  重建并附到 `musl-runtime-1.22.0` release 的路径。
- pdfium-native `0.6.1` 官方提供 `linux-musl-x64/arm64` 目标（SHA-256
  锁定），musl 平台包内的 PDFium addon 与 glibc 平台同源自构建。
- musl 平台为 CPU-only：WebGPU/Dawn 的 musl 支持另立资格矩阵，不在本
  版本范围（与 linux-arm64-gnu、windows-arm64 的 CPU-only 策略一致）。

## 已发布版本闭包

| 成熟度 | 包 | 已发布版本 | 最终标签 |
| --- | --- | ---: | --- |
| stable | `@arcships/light-ocr` | `0.5.8` | `latest`、`next` |
| stable | `@arcships/light-ocr-runtime` | `0.1.8` | `latest`、`next` |
| stable | 八个平台 native | `0.5.8` | `latest`、`next` |
| compatibility | `@arcships/light-ocr-document` | `0.1.4` | `next` |
| preview | `@arcships/light-ocr-tiny` | `0.1.7` | `next` |
| preview | `@arcships/light-ocr-medium` | `0.1.7` | `next` |

模型包没有重发新版本：Small 继续使用 `0.3.4`，Tiny/Medium 继续使用
`0.1.0`。发布流水线重建或取得这些不可变模型 tarball 完成完整离线安装
验证，并在确认 registry 同版本 integrity 完全一致后复用。

## 八平台 native 制品

下表来自发布候选 run 的 `release-manifest.json`，且 npm registry
integrity 已逐包核对一致。用户只安装当前平台对应的一个 native 包。

| 平台包 | 压缩 bytes | 解包 bytes | SHA-256 |
| --- | ---: | ---: | --- |
| 平台包 | 压缩 bytes | 解包 bytes | SHA-256 |
| --- | ---: | ---: | --- |
| `darwin-arm64` | 22,889,689 | 56,358,450 | `b073519dd23d3898f47272e79caa6e795c1e8b4210776daf7f52ca78d2283d69` |
| `darwin-x64` | 24,854,005 | 62,111,293 | `77258f6e33cdd8060fa930b35b647dd277ca1a6f928c1fa93f9ae6447508eb43` |
| `linux-arm64-gnu` | 20,453,594 | 40,262,556 | `b1e640f2f22908075e1c639957b7217d5fb983f4770c62f25a4a9fb0b3a61773` |
| `linux-x64-gnu` | 27,033,567 | 59,390,887 | `925d2a2a8097ed2266ab24b285fb2e9dafc17e8912a70d416b39cfb4a0991e26` |
| `linux-arm64-musl` | 22,839,920 | 49,120,781 | `a46665a54fdeebbdbc03bff372873050ccf58a156c84a4e8aa73ab836b4458eb` |
| `linux-x64-musl` | 24,712,425 | 56,327,142 | `6bb8bef38acaf8095b3f1b42f1d8c5394ae086ecb0525ed8b54399ca7e3177fd` |
| `win32-arm64` | 16,669,552 | 31,027,950 | `9a317d2e93fded0def42e0a42e24a7d277e845f062bef5430e4b6bc9b1c9cda9` |
| `win32-x64` | 30,386,113 | 63,065,642 | `0faf1a65b4dd4a7ff965fd99c3c80b619ab93ae63a3154f4bafae7c2856800b5` |

## 发布门

- [x] PR 的 Linux 原生构建与 workspace Node 测试
- [x] 两轮独立 review（构建/供应链、JS/发布链路、文档一致性）及修复验证
- [x] 15 个新 package identity 的 npm registry 空位检查
- [x] glibc 六平台从锁定源码重建 native 与 PDFium addon
- [x] musl 两平台在 alpine:3.22 容器从锁定源码重建（deps.lock SHA-256
  校验、`LIGHT_OCR_TARGET_LIBC=musl` 工具链门、dumpmachine 校验）
- [x] 八个平台离线、禁用安装脚本安装完整闭包
- [x] glibc 六平台真实图片 OCR 与非嵌入中文字体 PDF OCR
- [x] musl 两平台在 Alpine 容器真实图片 OCR 与内置 PDF OCR
- [x] 候选 tarball manifest、bytes、SHA-256 与 npm integrity 审计
- [x] 以 `publish_to_registry=true` 发布不可变候选到 `next`
- [x] 从 npm registry 回装并核对 integrity 与禁网运行
- [x] 将 stable Small/runtime/native 闭包晋升到 `latest`
- [x] 创建 `v0.5.8` GitHub Release

## 实际发布结果

1. [演练 run 36012260612](https://github.com/arcships/light-ocr/actions/runs/36012260612)
   的全部 job 全绿；15 包 manifest 审计通过且没有写入 registry。
2. [发布 run 36013862673](https://github.com/arcships/light-ocr/actions/runs/36013862673)
   重复八平台构建与离线 smoke，将新 package identity 发布到 `next`，再从
   registry 回装并验证 integrity、图片/PDF OCR 与禁网运行。首次 publish
   job 因 npm registry 对部分包的 integrity 传播超过 600 秒超时；重跑该
   单个 job 后按幂等语义跳过已发布包并完成剩余发布，最终全绿。
3. [晋升 run 36018294655](https://github.com/arcships/light-ocr/actions/runs/36018294655)
   只把 Small `0.5.8`、runtime `0.1.8` 与八个 native `0.5.8` 晋升到
   `latest`。Document、Tiny 与 Medium 继续保持 `next`（musl 首版由 npm
   规则自动带 `latest`，与 `next` 一致）。
4. [`v0.5.8` GitHub Release](https://github.com/arcships/light-ocr/releases/tag/v0.5.8)
   绑定正式发布来源提交，记录公开发布。

如需回滚，不覆盖或删除已发布版本；使用已归档的 `0.5.7` 发布 artifact
（run `30988312627`）将 stable 标签恢复到 Small/native `0.5.7` 与 runtime
`0.1.7`。

## GitHub Release

Release name：

`雪岭轻舟，musl 同渡 · Ship musl platform packages in 0.5.8`
