# npm 0.4.0 N2 候选记录

状态：本地工程完成，等待合并后的六平台 release run 与公共发布<br>
日期：2026-07-22

## 范围

`0.4.0` 完成 N2 package topology cutover。Small 仍是稳定默认；Tiny 和
Medium 以独立 `0.1.0` preview package 进入 `next`，不会被 promotion workflow
提升到 `latest`。Server 作为 private `0.1.1` workspace preview 一并维护，但不发布。

| 角色 | package/version | 默认模型或依赖 | channel |
| --- | --- | --- | --- |
| Stable facade | `@arcships/light-ocr@0.4.0` | Small model `0.3.4` + runtime `0.1.0` | `next` → `latest` |
| Shared runtime | `@arcships/light-ocr-runtime@0.1.0` | 六个 native `0.4.0` optional dependencies | `next` → `latest` |
| Tiny facade/model | `@arcships/light-ocr-tiny@0.1.0` / `...model-ppocrv6-tiny@0.1.0` | Tiny bundle | `next` only |
| Medium facade/model | `@arcships/light-ocr-medium@0.1.0` / `...model-ppocrv6-medium@0.1.0` | Medium bundle | `next` only |
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
Python model bootstrap 与 12-package assembler/pack tests。远端六平台 native/package
smoke 只能在合并后由手动 npm release workflow 产生，因此本记录不提前声称已发布。

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

## Promotion 边界

`npm-promote` 只提升六个 native、runtime 和 Small facade。Tiny/Medium 缺少明确
产品收益证据时继续留在 `next`；这不阻塞 N2 工程完成，也不阻塞进入 N3。
