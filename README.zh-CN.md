# light-ocr

[![Core CI](https://github.com/arcships/light-ocr/actions/workflows/core.yml/badge.svg?branch=main)](https://github.com/arcships/light-ocr/actions/workflows/core.yml)
[![License](https://img.shields.io/github/license/arcships/light-ocr)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](https://isocpp.org/)
[![Node--API v8](https://img.shields.io/badge/Node--API-v8-339933.svg)](bindings/node/README.md)
[![npm](https://img.shields.io/npm/v/%40arcships%2Flight-ocr?color=CB3837)](https://www.npmjs.com/package/@arcships/light-ocr)

[English](README.md) | 简体中文

![light-ocr 像素风宣传图](docs/assets/light-ocr-banner.png)

**面向 Node.js 与 C++ 的快速离线 OCR。**

直接在本机识别 JPEG、PNG 或像素数据，返回按阅读顺序排列的文字、置信度和四边形坐标。Node.js 用户安装的 npm 包内置 PP-OCRv6 Small 模型，并提供 macOS、Linux 和 Windows 的预编译组件。

## 快速开始

支持 Node.js 22 和 24。

```bash
npm install @arcships/light-ocr
```

```ts
import { createEngine } from "@arcships/light-ocr";
import { readFile } from "node:fs/promises";

const engine = await createEngine();
const result = await engine.recognizeEncoded(
  await readFile("image.jpg"),
);

for (const line of result.lines) {
  console.log(line.text, line.confidence, line.box);
}

await engine.close();
```

`createEngine()` 会根据当前平台自动选择合适的执行方式。如果应用已经完成图片解码，[`recognize()`](bindings/node/README.md#使用) 也可以直接接收 `GRAY8`、`RGB8`、`BGR8` 和 `RGBA8` 像素数据。

### 模型杯型

Small 继续作为稳定默认。N2 在 `next` tag 下增加两个可选 preview 包；三档使用完全相同的 API、类型、结果 schema 和错误模型，每次安装只携带所选的一份模型。

| 杯型 | Package / 命令 | 模型 payload | 状态 |
| --- | --- | ---: | --- |
| Small | `@arcships/light-ocr` / `light-ocr` | 约 30 MB | 稳定默认 |
| Tiny | `@arcships/light-ocr-tiny@next` / `light-ocr-tiny` | 约 6.3 MB | Preview；49 种语言，不含日语 |
| Medium | `@arcships/light-ocr-medium@next` / `light-ocr-medium` | 约 139 MB | Preview；精度优先 |

Tiny/Medium 只有在真实使用证明存在明确受众后才会提升 stable；它们不会改变 `npm install @arcships/light-ocr` 的安装内容。

## CLI

安装后即可使用 `light-ocr` 命令，无需额外配置：

```bash
# 识别文字 + 坐标
light-ocr image.png --format json

# 只要文字
light-ocr image.png --format text

# 只检测文字区域（不识别）
light-ocr detect image.png

# 区域识别
light-ocr recognize image.png --region 100,80,640,320 --format json

# 引擎信息
light-ocr info --version
```

三个子命令：`recognize`（默认）、`detect`（只检测框）、`info`（诊断）。输出使用 `schemaVersion: 1` 版本化 envelope，带稳定 line/detection ID。EXIF 方向自动修正。完整参考见 [CLI 设计](docs/cli-design.md) 和 [npm README](bindings/node/README.md#cli)。

## Agent Skill

内置 [Agent Skill](.agents/skills/local-ocr/SKILL.md)，供可调用本地命令的 AI Agent 使用。提供场景驱动的工作流、命令选择决策流和退出码参考：

- 何时使用 OCR 而非多模态模型
- 大图先 detect 再 recognize 的两步模式
- 票据/表单的 ROI 字段提取
- 用确定性 OCR 验证多模态模型输出

## 主要能力

- **本地处理。**图片和 OCR 结果始终留在本机。
- **只需安装一个包。**模型和当前平台的预编译组件会随 npm 包一起安装。
- **直接得到可用结果。**每一行都包含识别文字、置信度和原图位置。
- **默认使用硬件加速。**Auto 在 macOS 15+ Apple Silicon 上优先使用 Core ML，在下表的 Linux 和 Windows 版本中优先使用 WebGPU。
- **适合应用内调用。**识别任务在 JavaScript 主线程之外执行，并支持队列、取消和明确释放资源。
- **识别大图中的小字。**可选的 `tiled` 模式可以保留高分辨率图片中的小字和密集文字。

> ⭐ **觉得 light-ocr 有用？** 点个 Star，让更多人发现这个项目！

## 平台加速

npm 包提供以下六个平台版本。默认的 `createEngine()` 使用 Auto 模式：

| 平台 | Auto 模式 |
| --- | --- |
| macOS / Apple Silicon | macOS 15+ 优先使用 Core ML，然后使用 CPU |
| macOS / Intel | CPU |
| Linux x64 glibc | 通过 Vulkan 使用 WebGPU，然后使用 CPU |
| Linux arm64 glibc | CPU |
| Windows x64 | 通过 D3D12 使用 WebGPU，然后使用 CPU |
| Windows arm64 | CPU |

需要明确控制时，可以通过 [`execution` 选项](bindings/node/README.md#使用)选择 `auto`、`cpu`、`apple` 或 `webgpu`。

## 实测性能

0.3.0 在三台真实设备上完成了同机对比：

![light-ocr 0.3.0 同机速度与 OCR 进程 CPU time 降幅](docs/assets/light-ocr-0.3.0-performance-v2.png)

| 设备 | 加速方式 | 端到端加速 | OCR 进程 CPU time |
| --- | --- | ---: | ---: |
| Apple M4 Max | Core ML | `HELLO 123` **2.30×**；密集表单 **2.85×** | **降低 95.91%–97.67%** |
| Linux / NVIDIA RTX 5060 Ti | WebGPU / Vulkan | 14 张测试图片整体 **5.70×** | **降低 69.97%** |
| Windows / AMD Radeon 780M | WebGPU / D3D12 | 14 张测试图片整体 **2.44×** | **降低 46.33%** |

以上数字均为表中设备相对本机 CPU 路径的结果，并会随任务和硬件变化。14 张图片的整体加速比，是各图片 CPU 中位耗时之和除以 WebGPU 中位耗时之和。CPU 列统计同一批任务的 OCR 进程累计 CPU time，而不是某一瞬间的系统利用率；更低的累计 CPU time 意味着 OCR 运行期间能给前台应用留出更多 CPU 资源。Apple 测试通过了锁定的 CPU 一致性门槛；两组 WebGPU 测试的 14 张图片均与 CPU FP32 输出逐字节一致。完整测量和测试方法见 [0.3.0 发布报告](docs/releases/npm-0.3.0.md)。

## C++

C++ 项目从源码构建静态库，并链接 `light_ocr::core` CMake target。API 可以直接识别解码后的 `GRAY8`、`RGB8`、`BGR8` 或 `RGBA8` 像素；接入方式见 [C++ API](docs/native-api.md)，各平台准备方式见 [构建说明](docs/build-and-release.md)。

## 文档

- [CLI 参考](docs/cli-design.md)
- [Node.js API 与示例](bindings/node/README.md)
- [Agent Skill](.agents/skills/local-ocr/SKILL.md)
- [C++ API](docs/native-api.md)
- [Apple Silicon 加速](docs/apple-device-acceleration.md)
- [Linux WebGPU 加速](docs/linux-device-acceleration.md)
- [Windows WebGPU 加速](docs/windows-device-acceleration.md)
- [模型包](docs/model-bundle.md)
- [构建与发布](docs/build-and-release.md)
- [路线图](docs/roadmap.md)
- [更新日志](CHANGELOG.md)
- [npm 0.3.0 发布报告](docs/releases/npm-0.3.0.md)
- [npm 0.4.0 N2 候选记录](docs/releases/npm-0.4.0.md)

## 社区与协议

欢迎提交 issue 和 pull request — 请先阅读 [CONTRIBUTING.md](CONTRIBUTING.md) 了解贡献指南。所有参与者须遵守[行为准则](CODE_OF_CONDUCT.md)。

`light-ocr` 使用 [Apache License 2.0](LICENSE)。
