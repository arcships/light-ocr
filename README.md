# light-ocr

[![Core CI](https://github.com/arcships/light-ocr/actions/workflows/core.yml/badge.svg?branch=main)](https://github.com/arcships/light-ocr/actions/workflows/core.yml)
[![License](https://img.shields.io/github/license/arcships/light-ocr)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](https://isocpp.org/)
[![Node--API v8](https://img.shields.io/badge/Node--API-v8-339933.svg)](bindings/node/README.md)
[![npm](https://img.shields.io/npm/v/%40arcships%2Flight-ocr?color=CB3837)](https://www.npmjs.com/package/@arcships/light-ocr)

<a href="https://trendshift.io/repositories/82168?utm_source=trendshift-badge&amp;utm_medium=badge&amp;utm_campaign=badge-trendshift-82168" target="_blank" rel="noopener noreferrer"><img src="https://trendshift.io/api/badge/trendshift/repositories/82168/daily?language=C%2B%2B" alt="arcships%2Flight-ocr | Trendshift" width="250" height="55"/></a>

English | [简体中文](README.zh-CN.md)

![light-ocr pixel-art banner](docs/assets/light-ocr-banner.png)

**Fast, offline OCR for Node.js and C++.**

Recognize text in JPEG, PNG, or raw image data directly on your machine. `light-ocr` returns lines in reading order with confidence scores and quadrilateral coordinates. For Node.js, the npm package includes PP-OCRv6 Small and prebuilt components for macOS, Linux, and Windows.

## Quick start

Node.js 22 and 24 are supported.

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

`createEngine()` automatically chooses the right execution mode for the current platform. If your application already decodes images, [`recognize()`](bindings/node/README.md#使用) also accepts `GRAY8`, `RGB8`, `BGR8`, and `RGBA8` pixel data.

## What you get

- **Local processing.** Images and OCR results stay on your machine.
- **One package to install.** The model and matching prebuilt component are included with the npm package.
- **Useful output.** Every line includes recognized text, confidence, and its position in the original image.
- **Hardware acceleration by default.** Auto tries Core ML first on macOS 15+ Apple Silicon, and WebGPU first on the Linux and Windows builds below.
- **Application-friendly execution.** Recognition runs off the JavaScript main thread and supports queues, cancellation, and explicit cleanup.
- **Small text in large images.** An optional `tiled` mode preserves small and dense text in high-resolution images.

> ⭐ **Like light-ocr?** Give it a star — it helps others discover the project and keeps us motivated!

## Platform acceleration

The npm package provides the following four builds. The default `createEngine()` call uses Auto mode:

| Platform | Auto mode |
| --- | --- |
| macOS on Apple Silicon | Core ML on macOS 15+, then CPU |
| macOS on Intel | CPU |
| Linux x64 with glibc | WebGPU through Vulkan, then CPU |
| Windows x64 | WebGPU through D3D12, then CPU |

Applications that need explicit control can choose `auto`, `cpu`, `apple`, or `webgpu` through the [`execution` option](bindings/node/README.md#使用).

## Measured performance

Version 0.3.0 was measured on three real devices:

![light-ocr 0.3.0 same-device speed and OCR process CPU-time reductions](docs/assets/light-ocr-0.3.0-performance-v2.png)

| Device | Acceleration | End-to-end speedup | OCR process CPU time |
| --- | --- | ---: | ---: |
| Apple M4 Max | Core ML | **2.30×** on `HELLO 123`; **2.85×** on a dense form | **95.91%–97.67% less** |
| NVIDIA RTX 5060 Ti on Linux | WebGPU / Vulkan | **5.70×** overall across 14 test images | **69.97% less** |
| AMD Radeon 780M on Windows | WebGPU / D3D12 | **2.44×** overall across 14 test images | **46.33% less** |

These are same-machine comparisons with the CPU path, and results vary by workload and hardware. For the 14-image results, overall speedup is the sum of the per-image CPU median times divided by the sum of the WebGPU median times. The CPU column measures cumulative OCR process CPU time over the same workloads, rather than an instantaneous system-utilization sample; lower CPU time leaves more capacity for the rest of the application while OCR is active. The Apple run passed its locked CPU-parity thresholds; both WebGPU runs were byte-identical to CPU FP32 on all 14 images. See the [0.3.0 release report](docs/releases/npm-0.3.0.md) for complete measurements and methodology.

## C++

C++ projects build the static library from source and link the `light_ocr::core` CMake target. The API accepts decoded `GRAY8`, `RGB8`, `BGR8`, or `RGBA8` pixels; start with the [C++ API guide](docs/native-api.md) and [build instructions](docs/build-and-release.md).

## Documentation

- [Node.js API and examples](bindings/node/README.md)
- [C++ API](docs/native-api.md)
- [Apple Silicon acceleration](docs/apple-device-acceleration.md)
- [Linux WebGPU acceleration](docs/linux-device-acceleration.md)
- [Windows WebGPU acceleration](docs/windows-device-acceleration.md)
- [Model bundle](docs/model-bundle.md)
- [Build and release](docs/build-and-release.md)
- [Changelog](CHANGELOG.md)
- [npm 0.3.0 release report](docs/releases/npm-0.3.0.md)

## Community and license

Issues and pull requests are welcome — see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines. All participants are expected to follow our [Code of Conduct](CODE_OF_CONDUCT.md).

`light-ocr` is available under the [Apache License 2.0](LICENSE).
