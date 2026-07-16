# light-ocr

[![Core CI](https://github.com/arcships/light-ocr/actions/workflows/core.yml/badge.svg?branch=main)](https://github.com/arcships/light-ocr/actions/workflows/core.yml)
[![License](https://img.shields.io/github/license/arcships/light-ocr)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](https://isocpp.org/)
[![Node--API v8](https://img.shields.io/badge/Node--API-v8-339933.svg)](bindings/node/README.md)
[![npm](https://img.shields.io/npm/v/%40arcships%2Flight-ocr?color=CB3837)](https://www.npmjs.com/package/@arcships/light-ocr)

[English](README.md) | 简体中文

![light-ocr 像素风宣传图](docs/assets/light-ocr-banner.png)

**为原生应用和 Node.js 应用准备的离线 OCR，由 PP-OCRv6 Small 驱动。**

`light-ocr` 在应用自己的进程内，把图片转换为按阅读顺序排列的文字、置信度和四边形位置。它不需要把图片上传到云端，也不需要额外运行 Python 服务。原生 Core 接受解码后的像素；`main` 上的 Node.js 适配器还可以直接接收内存中的 JPEG 和 PNG bytes。

这个项目面向希望把 OCR 做成真正本地能力的产品：随时调用、默认保护隐私，也能自然嵌入现有的图像处理流程。

> **npm 已可用：**`@arcships/light-ocr@0.2.0` 自带默认 PP-OCRv6 Small 模型和全部 Tier 1 平台的预编译原生运行时，并支持可选 tiled 检测与 Node.js 内存 JPEG/PNG 直接输入。详见[包支持](#包支持)。

> **`main` 上的 Apple 加速：**即将发布的 `0.2.1` 源码候选加入 macOS 15+ 可选 Direct Core ML 路径。在已完成资格验证的 Apple M4 Max 上，相比 12 线程 CPU profile，两个锁定 workload 取得 **2.30×–2.85× 的预热端到端加速**，OCR 进程 CPU time 降低 **95.91%–97.67%**。该 provider 尚未进入已经发布的 `0.2.0` npm packages。

## 适合哪些场景

| 应用场景 | light-ocr 能提供什么 |
| --- | --- |
| **桌面端与本地优先应用** | 从截图、框选区域、剪贴板图片、笔记和导入页面中提取文字，不上传用户内容。 |
| **私有文档流程** | 在应用完成渲染或解码后，识别扫描表单、票据、标签和内部文档中的文字。 |
| **图片、相机与媒体工具** | 在现有像素流程中加入全文搜索、复制文字、画面标注、内容索引或无障碍能力。 |
| **本地部署与边缘软件** | 在自助终端、设备、边缘节点或受控网络中运行一致的 OCR 模型，摆脱云服务依赖。 |
| **原生与 Node.js 服务** | 把 OCR 直接嵌入应用，不再单独部署和维护 Python 进程或 OCR daemon。 |

当前模型主要面向常规文字检测和 CJK/拉丁字符混排识别。Node.js 适配器可以解码内存中的 JPEG/PNG；原生 Core 仍只接受解码后的像素。PDF 渲染、其他图片格式、文档版面分析、表格、公式和翻译仍由宿主应用负责。

## 为什么要做 light-ocr

云 OCR 使用方便，但也带来了图片上传、网络可用性、持续成本和新的隐私边界。操作系统 OCR API 不依赖网络，但各个平台的能力与行为并不一致。PaddleOCR 提供了优秀的模型，不过常见的 Python 部署方式并不总适合桌面软件、原生产品和 Node.js 应用。

`light-ocr` 希望补上这块空白：围绕官方 PP-OCRv6 Small 模型，提供一套可复用的原生核心。应用继续掌控任务调度、数据存储和用户体验；light-ocr 在保留原生 raw-pixel 边界的同时，把图片稳定地转换为结构化 OCR 结果。

## light-ocr 的优势

- **默认本地运行。**识别过程不会访问网络，也不会启动子进程。
- **适合真实应用流程。**直接接收 `GRAY8`、`RGB8`、`BGR8` 和 `RGBA8` 像素；Node.js 适配器也能解码已经在内存中的 JPEG 和 PNG。
- **两种明确的大图策略。**bounded/960 仍是速度和内存优先的默认模式；可选 tiled 检测为小字和密集的 2048 像素文档保留更多细节，并始终逐个处理 detection tile。
- **按需启用原生 Apple 加速。**`0.2.1` 源码候选可以用 Core ML 执行 FP16 detection/recognition，同时保持 CPU 默认行为和公共 OCR 结果契约不变。
- **模型固定且可复现。**约 31 MB 的 PP-OCRv6 Small bundle 会经过完整性验证，目标是随应用一起安装，而不是首次运行时再下载。
- **跨平台结果一致。**macOS、Linux 和 Windows 使用同一套模型与结果契约。
- **适合异步宿主。**Node-API 适配器不会占用 JavaScript 主线程，并提供有界队列、取消和明确的生命周期控制。
- **开放、可检查。**项目采用 Apache-2.0 协议，并在 CI 中验证真实模型行为、大图内存、生命周期安全和输出对齐。

## 为什么选择 PP-OCRv6 Small

![PP-OCRv6 模型与 VLM 官方准确率对比](docs/assets/ppocrv6-model-comparison.png)

npm package 使用 **PP-OCRv6 Small**。在 PaddleOCR 的内部多场景基准中，它取得了 **84.1 的检测 Hmean** 和 **81.3 的识别加权准确率**，同时保持适合本地应用的模型规模。图表和准确率来自 [PP-OCRv6 官方评测](https://github.com/PaddlePaddle/PaddleOCR/blob/211989f046cc1878460f9e65574690c00a127a1a/docs/version3.x/algorithm/PP-OCRv6/PP-OCRv6.md)；这是上游质量结果，不是本项目测得的耗时。

## 返回结果是什么样的

对于每一行检测到的文字，light-ocr 都会返回识别文本、置信度，以及它在原图中的位置：

```json
{
  "lines": [
    {
      "text": "HELLO 123",
      "confidence": 0.99,
      "box": [
        {"x": 106, "y": 54},
        {"x": 554, "y": 54},
        {"x": 554, "y": 135},
        {"x": 106, "y": 135}
      ]
    }
  ]
}
```

位置使用四边形而不是普通矩形，因此可以保留旋转文字和透视文字的几何信息。

## 实测速度

![800×180 的 HELLO 123 benchmark 输入图](docs/assets/benchmark-generated-hello-123.png)

### CPU 基线

对于上面这张 `800×180` BGR 图片，light-ocr 识别结果为 `HELLO 123`，置信度 `0.9893`。原生 C++ Release benchmark 运行在 Apple M4 Max（16 核 CPU、128 GB 内存）、macOS 26.5.1、ONNX Runtime CPU 环境，intra-op 与 inter-op thread 均为 1，并使用默认 bounded/960 策略和 recognition batch size 1。

| 指标 | 实测结果 |
| --- | ---: |
| 预热后端到端中位数 | **75.678 ms/张**（约 13.2 张/秒） |
| 预热后端到端 P95 | **79.788 ms/张** |
| 检测 + 识别纯推理中位数 | **74.125 ms/张** |
| 模型 bundle 加载，仅一次 | 167.906 ms |
| Engine 初始化，仅一次 | 30.511 ms |

测试先预热 5 次，再测量 30 次。这是一张小尺寸、合成的单行图片；实际延迟会随硬件、图片尺寸、文字密度和文本行数变化。benchmark 契约及与固定 Python oracle 的对照见[实施状态](docs/implementation-status.md#本机最终验证快照)。

### Apple Core ML 加速

`0.2.1` provider Gate 在一台 Apple M4 Max（16 核 CPU、128 GB 内存，macOS 26.5.1）上，对比了显式启用的 FP16 Core ML 路径和 `cpu_fast` profile。CPU profile 最多使用 12 个 intra-op threads；每个 workload 先预热 5 次，再进行 3 组独立的 30 次测量。CPU time 降幅表示宿主进程占用变化，不是能耗数据。

| 锁定 workload | CPU 预热 P50 | Apple 预热 P50 | 端到端加速 | OCR 进程 CPU time 降幅 |
| --- | ---: | ---: | ---: | ---: |
| 合成 `HELLO 123`，800×180 | 19.774 ms | **8.599 ms** | **2.300×** | **95.91%** |
| XFUND 密集表单，113 行文字 | 943.627 ms | **331.011 ms** | **2.851×** | **97.67%** |

加速输出同时通过全部 14 个锁定质量 fixtures：相对 CPU oracle 的字符相似度为 99.6484%，detection recall 为 100%，匹配框平均 IoU 为 99.5508%，匹配结果平均置信度差为 0.004349，critical failure 为 0。这些是相对 CPU 输出的 parity 指标，不是独立 ground-truth 准确率；FP16 输出也并非逐字节一致。

正式 warm 性能测量的 peak RSS 为 692.14 MiB，自包含 Apple 模型 payload 增加 25.42 MiB。独立的同 engine 100 个密集页生命周期测试 peak RSS 为 888.11 MiB，结束时比预热后基线低 27.47 MiB，该次测试未出现持续增长。首次使用会离线编译，并按需加载 recognition functions：固定 `HELLO 123` 启动 canary 的 compiled-cache miss 为 7.219 s，hit 为 1.275/1.278 s；113 行表单的首次整页 miss 为 53.846 s，hit 为 12.677/12.677 s。运行时不会下载 provider、编译器或模型。

真实设备性能数据只来自这一台 M4 Max。证据契约把它归入 `Apple M4` device family 并据此设置 `deviceValidated`，不代表每一种 M4 SKU 都做过独立测量。该候选对其他 macOS 15+ 硬件采取开放但实验性的兼容策略：M1–M3 和后续 Apple Silicon 可以尝试同一 ANE/GPU 路径，Intel Mac 使用 Core ML CPU+GPU。没有已审阅证据的硬件会报告 `deviceValidated: false`；取得对应设备数据前不宣称加速倍数。完整方法、模型放置、质量阈值、缓存与生命周期结果见 [Apple 加速技术方案](docs/apple-device-acceleration.md)。

## 开始使用

### Node.js

Node.js 22 和 24 支持 macOS arm64/x64、Linux x64 glibc 与 Windows x64：

```bash
npm install @arcships/light-ocr
```

安装会自动取得当前平台的原生运行时和固定版本的 PP-OCRv6 Small 模型；首次运行不会再下载模型，`postinstall` 也不会现场编译原生代码。0.2.0 同时支持下面的 `recognizeEncoded()` 和 raw-pixel `recognize()`。

```ts
import { createEngine } from "@arcships/light-ocr";
import { readFile } from "node:fs/promises";

const engine = await createEngine();
const result = await engine.recognizeEncoded(
  await readFile("image.jpg"),
);

// 如果宿主已有图片解码流程，仍可直接传入 raw pixels。
const rawResult = await engine.recognize({
  data: pixels,
  width,
  height,
  stride,
  pixelFormat: "rgba8",
});

console.log(result.lines);
console.log(rawResult.lines);
await engine.close();
```

Apple provider 仍需显式启用。下面是面向维护者/源码 checkout 的 preview，不是 `0.2.0` 的 `npm install` 使用路径；它假设已经按照[构建与发布](docs/build-and-release.md#8-ci)中的本机 release tooling 派生出锁定的自包含 Apple bundle。请求 Apple 路径时应保留可观测的整 session CPU fallback：

```ts
const engine = await createEngine({
  // 仅源码候选需要；计划中的 npm 0.2.1 package 会自动解析该 payload。
  bundlePath: "/absolute/path/to/ppocrv6-small-apple-20260715.1",
  execution: {
    provider: "apple",
    precision: "fp16",
    cpuPartition: "allow",
    sessionFallback: "cpu",
  },
});

console.log(engine.info.execution.sessions.detection.deviceValidated);
```

`cpuPartition: "allow"` 同时适用于 Apple Silicon 和 Intel Mac；strict GPU-only profile 只支持 Apple Silicon。普通 npm 用户应在 Apple payload 正式发布前继续使用默认 CPU provider。

完整 API、取消、队列限制和生命周期行为见 [Node.js 指南](bindings/node/README.md)。

### C++ Core

构建需要 Python 3（仅用于 bootstrap 工具）、CMake 和支持 C++17 的编译器。依赖与模型输入均已锁定；构建后的运行时不依赖 Python。

```bash
python3 tools/bootstrap_dependencies.py --cache-dir .cache/dependencies
python3 tools/bootstrap_models.py --cache-dir .cache/models

cmake --preset release \
  -DLIGHT_OCR_DEPENDENCY_CACHE_DIR="$PWD/.cache/dependencies"
cmake --build --preset release --parallel
ctest --preset release
```

各平台的准备方式见[构建与发布](docs/build-and-release.md)，接入方式见 [C++ API](docs/native-api.md)。

## 包支持

| 分发方式 | 当前状态 | 平台 |
| --- | --- | --- |
| C++ Core 源码 | 可用 | macOS arm64/x64、Linux x64 glibc、Windows x64 |
| Node-API 适配器源码 | 可用 | Node.js 22 和 24 |
| [`@arcships/light-ocr`](https://www.npmjs.com/package/@arcships/light-ocr) | 已发布 `0.2.0` | 全部 Tier 1 平台的 Node.js 22/24 |
| [`@arcships/light-ocr-model-ppocrv6-small`](https://www.npmjs.com/package/@arcships/light-ocr-model-ppocrv6-small) | 已发布 `0.2.0` | 与平台无关的必需模型依赖 |
| 各平台 native npm packages | 已发布 `0.2.0` | macOS arm64/x64、Linux x64 glibc、Windows x64 |

npm 分发会安装一个统一入口、一个必需的模型包，以及与当前系统匹配的 native 包。包内容、版本策略和发布门槛见 [npm package 设计](docs/npm-packaging.md)；`0.2.0` 的不可变哈希和验证证据见[发布记录](docs/releases/npm-0.2.0.md)。

Direct Core ML 加速已经合并到 `main`，目标版本为 `0.2.1`，但尚未进入已发布的 `0.2.0` package set。它会继续复用现有六包安装结构，不计划新增 provider package 或运行时下载。

## 项目状态

`light-ocr` 仍在积极开发。`0.2.0` 已发布确定性的 `tiled-v1` 大图模式，以及 Node.js 适配器中受资源限制的内存 JPEG/PNG 解码；C++ Core 的 raw-pixel 边界保持不变。当前 `0.2.1` 源码候选加入 macOS 可选 Direct Core ML 执行，同时继续以 CPU 为默认 provider。

作为 pre-1.0 项目，公共 API 和 package 布局仍可能调整；项目目前不承诺跨版本稳定的 C++ ABI。

Core CI 当前覆盖：

- macOS arm64
- macOS x64
- Linux x64 glibc
- Windows x64

CI 还会执行 sanitizer、fuzz smoke、离线运行、输出对齐、质量和内存门槛。性能资格审查使用独立、显式触发的 workflow，不属于普通 CI 或发布预检。已验证结果和当前缺口见[实施状态](docs/implementation-status.md)。

## 文档

- [更新日志](CHANGELOG.md)
- [C++ API](docs/native-api.md)
- [Node.js 适配器](bindings/node/README.md)
- [构建与发布](docs/build-and-release.md)
- [模型 bundle](docs/model-bundle.md)
- [准确率与输出对齐](docs/parity-testing.md)
- [大图内存表现](docs/memory-optimization.md)
- [架构](docs/architecture.md)
- [实施状态](docs/implementation-status.md)

## 参与社区

欢迎提交 issue 和 pull request。如果你正在评估 light-ocr 是否适合自己的产品，可以[创建 issue](https://github.com/arcships/light-ocr/issues)，告诉我们目标平台、图片来源、语言组合和大致负载。真实应用场景会直接影响 package 优先级和后续模型支持。

报告问题时，请尽量提供平台、输入尺寸、像素格式、模型 bundle ID 和最小复现。除非你确认可以公开，否则不要上传包含隐私信息的原始图片。

## 开源协议

light-ocr 使用 [Apache License 2.0](LICENSE)。第三方依赖和模型 notice 会随对应的发布制品一起提供。
