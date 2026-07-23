# npm 0.4.0 N2 发布记录

状态：已发布；Small 稳定闭包已晋级 `latest`<br>
日期：2026-07-23

## 发布身份

- Release run：
  [`29998726639`](https://github.com/arcships/light-ocr/actions/runs/29998726639)，
  source `221fd80587acfd40647b3874ff9daf2dfa77af22`，成功，35m23s。
- Promotion run：
  [`30001769602`](https://github.com/arcships/light-ocr/actions/runs/30001769602)，
  source `575a456b2af95d14bed85621eac4387aa3dccdf9`，成功，2m44s。
- Release artifact：`light-ocr-npm-0.4.0`，GitHub artifact size 225 MB，
  digest
  `sha256:d36f34f88b981699cb014e97df4e83fed885eb712018457bb124ef5febb6c710`。
- 发布集合：12 个新版本；Server workspace 为 private preview，不在 artifact
  或 registry 中。

## 范围

`0.4.0` 完成 N2 package topology cutover。Small 仍是稳定默认；Tiny 和
Medium 以独立 `0.1.0` preview package 请求发布到 `next`。npm 对首次发布的新
package name 同时创建并要求保留 `latest`，因此这四个 preview facade/model 的
`latest` 也指向它们唯一的 `0.1.0`；这不是项目的稳定性晋级。它们在 package
metadata、README 和 CLI 中继续声明 `maturity: "preview"`，promotion workflow
不会主动推进这些 tag。Server 作为 private `0.1.1` workspace preview 一并维护，
但不发布。

| 角色 | package/version | 默认模型或依赖 | channel |
| --- | --- | --- | --- |
| Stable facade | `@arcships/light-ocr@0.4.0` | Small model `0.3.4` + runtime `0.1.0` | `next` → `latest` |
| Shared runtime | `@arcships/light-ocr-runtime@0.1.0` | 六个 native `0.4.0` optional dependencies | `next` → `latest` |
| Tiny facade/model | `@arcships/light-ocr-tiny@0.1.0` / `...model-ppocrv6-tiny@0.1.0` | Tiny bundle | requested `next`; registry-required first-version `latest`; preview |
| Medium facade/model | `@arcships/light-ocr-medium@0.1.0` / `...model-ppocrv6-medium@0.1.0` | Medium bundle | requested `next`; registry-required first-version `latest`; preview |
| Native runtime | six platform packages `0.4.0` | Core `0.4.0` | `next` → `latest` |

三档 facade 共享 runtime 的 API、types、`OcrError`、CLI parser、EXIF、结果 schema
和 native loader。只有 Small 拥有 `light-ocr`；另外两个命令分别为
`light-ocr-tiny` 和 `light-ocr-medium`。安装任一 facade 只精确依赖一个模型。

## 锁定模型

| Tier | Bundle ID | archive bytes | SHA-256 | 语言限制 |
| --- | --- | ---: | --- | --- |
| Tiny | `ppocrv6-tiny-onnx-20260722.1` | 6,369,280 | `8c89c3e9a024070e828a8e682b92c2b6942fe39868e31ea4fcc95195c21869ff` | 49 languages；不含 `ja` |
| Small | `ppocrv6-small-onnx-20260714.2` | 31,334,400 | `e543b93bc4882f35b1564a71961e5bc55439ede6c2f33b4166acc15e6348712f` | 50 languages |
| Medium | `ppocrv6-medium-onnx-20260722.1` | 138,885,120 | `496bab7e76e3d8b94f20cdafc3787471c04ff4cfea360de3e5f7dec7bbfb4e39` | 50 languages |

Tiny/Medium npm model packages包含 bundle license、`SHA256SUMS`、SPDX SBOM、
`artifact-hashes.json` 和 npm integrity。Small 复用已发布的不可变 `0.3.4`
模型 package，不为 facade/version 变化重新复制 31 MB payload。

公共 npm model package 的实际体积：

| Tier | Tarball bytes | Unpacked bytes | npm integrity |
| --- | ---: | ---: | --- |
| Tiny `0.1.0` | 5,750,942 | 6,377,661 | `sha512-AIV0oRSKADvAmguBJwFtPo+Si+p/ECbNTUvmmmZv+DqNOOO58txv8+JMBi486di2rXStsVuAcwtdp7ZYtUgMhQ==` |
| Small `0.3.4` | 52,529,118 | 73,594,252 | `sha512-F2Rjx3xoiKw1eUc/DW5k5/t1AlvWfReUDTdpRdQKNCDz5Y8YYif+15CHdPz7Wdt/Nza7drDOM3NTGUIuA20ERw==` |
| Medium `0.1.0` | 99,999,057 | 138,888,677 | `sha512-+xEqZFzgrO8BjF0lAiWjxUyHqZTu95qwln17Gaccfc4GgL91F22N1cjz9v1EykSqlG7YZOoHvfyZrlwGRZLwQg==` |

## 本机证据

环境：macOS arm64 Apple M4 Max；新构建 Core/Node addon `0.4.0`；同一
`generated-hello-123` fixture。该表是一次功能 canary，不是公开性能承诺。

| Tier | session 初始化 | 单次 OCR | 结果 |
| --- | ---: | ---: | --- |
| Tiny | ~894 ms | ~58 ms | `HELLO 123` |
| Small | ~1,484 ms | ~112 ms | `HELLO 123` |
| Medium | ~6,449 ms | ~457 ms | `HELLO 123` |

此外已完成：Core 三档 manifest/profile 解析 unit test、Tiny/Medium 真实 engine
smoke、workspace TypeScript/Node/server contracts、CLI 56 项 contract/EXIF tests、
Python model bootstrap 与 12-package assembler/pack tests。正式 release run 的六个
native build 与六个平台 Small 安装/OCR smoke 全部成功；Linux x64 另外执行
Tiny/Medium tarball 安装与真实 OCR。

发布后又在 macOS arm64、Node `v22.13.0` 的三个全新目录直接从公共 npm registry
安装并执行 `generated-hello-123`：

| 安装入口 | 实际闭包 | CLI / API OCR |
| --- | --- | --- |
| `@arcships/light-ocr@next`，随后无 tag 的 `@arcships/light-ocr` | facade `0.4.0` + runtime `0.1.0` + Small model `0.3.4` | `HELLO 123` |
| `@arcships/light-ocr-tiny@next` | facade/model `0.1.0` + runtime `0.1.0` | `HELLO 123`；`maturity: preview` |
| `@arcships/light-ocr-medium@next` | facade/model `0.1.0` + runtime `0.1.0` | `HELLO 123`；`maturity: preview` |

## CI / release 简化

- 版本来源移到根 `VERSION`；单纯版本/包/文档改动不触发完整 Core workflow。
- 普通 workspace CI 运行 types、package/server contracts 和 Python packaging tests。
- Core 只在原生源码、模型契约、原生构建链或对应测试变化时运行。
- npm release 不重跑 sanitizer、fuzz、oracle、provider qualification、Apple/WebGPU
  模型转换或 Core CTest；这些属于变更/资格 workflow。
- 六个平台只构建不可复用的 native payload；Tiny/Medium bundle 在 assembly job 各
  生成一次；每个 package 只 `npm pack` 一次并核对 inventory/integrity。
- 六平台各跑一条离线 Small OCR；Linux x64 再跑 Tiny/Medium。删除重复本地 registry
  和双重压包步骤。
- 本次成功 run 的 `publish` job 用时 25m34s，主要原因是每个新 package 发布后
  串行等待 npm registry metadata 收敛。发布后已改为同一 phase 先提交整批 package，
  再统一轮询 integrity；未来等待时间由最慢 package 决定，不再把每个 CDN 延迟相加。

## Promotion 边界

`npm-promote` 只主动提升六个 native、runtime 和 Small facade。Tiny/Medium
缺少明确产品收益证据时继续保持 preview maturity；未来 preview 版本只更新
`next`，得到 G2 证据后才主动移动 `latest`。对于首次发布的 package name，公共
npm registry 的 package schema 要求至少保留一个 `latest`，所以当前唯一的
`0.1.0` 同时出现在 `next` 和 `latest`，不能用删除 tag 的方式表达 preview。

发布过程中有两个安全失败：

- [`29997787639`](https://github.com/arcships/light-ocr/actions/runs/29997787639)
  在任何 publish job 前发现 model-free native metadata 仍要求 Small archive，
  随后取消；registry 未产生半发布版本。
- [`30001351476`](https://github.com/arcships/light-ocr/actions/runs/30001351476)
  尝试删除 npm 强制保留的 preview `latest` 时收到 HTTP 400，在任何稳定 tag
  变化前失败。修正 registry 语义后，最终 promotion run 成功。

这些边界不阻塞 N2 工程完成，也不阻塞进入 N3。
