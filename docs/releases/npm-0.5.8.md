# npm 0.5.8 发布记录

[English version](npm-0.5.8.en.md)

状态：发布准备中。版本闭包已同步到 `main`；八平台发布演练进行中
（[run 36000485825](https://github.com/arcships/light-ocr/actions/runs/36000485825)，
`publish_to_registry=false`）。

发布身份：

- musl 平台与 engines 放宽实现：
  [PR #63](https://github.com/arcships/light-ocr/pull/63)
  （含两轮独立 review 修复）
- musl ONNX Runtime 产物 release：
  [`musl-runtime-1.22.0`](https://github.com/arcships/light-ocr/releases/tag/musl-runtime-1.22.0)
- 版本同步提交：
  [`947ee35`](https://github.com/arcships/light-ocr/commit/947ee3518b521a1cf76ba5169ebf4e8ef2e1ee9d0)
- 八平台发布演练：
  [36000485825](https://github.com/arcships/light-ocr/actions/runs/36000485825)
  （`publish_to_registry=false`）
- registry 发布与回装验证：发布后回填
- 稳定 dist-tag 晋升：发布后回填
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
| PENDING-manifest-table | | | |

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
- [ ] 以 `publish_to_registry=true` 发布不可变候选到 `next`
- [ ] 从 npm registry 回装并核对 integrity 与禁网运行
- [ ] 将 stable Small/runtime/native 闭包晋升到 `latest`
- [ ] 创建 `v0.5.8` GitHub Release

## 实际发布结果

发布后回填：发布 run、晋升 run 与回滚路径。

## GitHub Release

Release name：

`雪岭轻舟，musl 同渡 · Ship musl platform packages in 0.5.8`
