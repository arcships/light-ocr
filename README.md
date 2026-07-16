# light-ocr

[![Core CI](https://github.com/arcships/light-ocr/actions/workflows/core.yml/badge.svg?branch=main)](https://github.com/arcships/light-ocr/actions/workflows/core.yml)
[![License](https://img.shields.io/github/license/arcships/light-ocr)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](https://isocpp.org/)
[![Node--API v8](https://img.shields.io/badge/Node--API-v8-339933.svg)](bindings/node/README.md)
[![npm](https://img.shields.io/npm/v/%40arcships%2Flight-ocr?color=CB3837)](https://www.npmjs.com/package/@arcships/light-ocr)

English | [简体中文](README.zh-CN.md)

![light-ocr pixel-art banner](docs/assets/light-ocr-banner.png)

**Offline OCR for native and Node.js applications, powered by PP-OCRv6 Small.**

`light-ocr` turns images into ordered text lines, confidence scores, and quadrilateral boxes—inside your own process, without sending an image to a cloud service or running a Python sidecar. The native Core accepts decoded pixels; the Node.js adapter on `main` also accepts in-memory JPEG and PNG bytes.

It is made for products where OCR should feel like a local capability: quick to invoke, private by default, and straightforward to embed into an existing image pipeline.

> **Available on npm:** `@arcships/light-ocr@0.2.0` includes the default PP-OCRv6 Small model, prebuilt native runtimes for all Tier 1 platforms, opt-in tiled detection, and direct in-memory JPEG/PNG input for Node.js. See [Package support](#package-support).

> **Apple acceleration on `main`:** the upcoming `0.2.1` source candidate adds an opt-in Direct Core ML path for macOS 15+. Across two locked workloads on the qualified Apple M4 Max, it delivered a **2.30×–2.85× warm end-to-end speedup** over the 12-thread CPU profile while reducing OCR process CPU time by **95.91%–97.67%**. This provider is not included in the published `0.2.0` npm packages yet.

## Where light-ocr fits

| Use case | What light-ocr provides |
| --- | --- |
| **Desktop and local-first apps** | Extract text from screenshots, selections, clipboard images, notes, and imported pages without uploading user content. |
| **Private document workflows** | Read text from scanned forms, receipts, labels, and internal documents after your application renders or decodes them. |
| **Image, camera, and media tools** | Add searchable text, copy-text actions, overlays, indexing, or accessibility features to an existing pixel pipeline. |
| **On-premise and edge software** | Run a consistent OCR model in kiosks, terminals, appliances, or controlled networks where a cloud dependency is undesirable. |
| **Native and Node.js services** | Embed OCR directly instead of deploying and supervising a separate Python process or OCR daemon. |

The current model is best suited to general text detection and recognition in CJK/Latin mixed content. The Node.js adapter can decode in-memory JPEG and PNG inputs; the native core still accepts decoded pixels only. PDF rendering, other image formats, document layout analysis, tables, formulas, and translation remain the host application's responsibility.

## Why this project exists

Cloud OCR is convenient, but it introduces uploads, network availability, recurring cost, and a new privacy boundary. Operating-system OCR APIs avoid the network, but their behavior and availability vary by platform. PaddleOCR offers excellent models, while its usual Python deployment is not always a natural fit for desktop software, native products, or a Node.js application.

`light-ocr` closes that gap with one reusable native core built around official PP-OCRv6 Small models. Applications keep control of scheduling, storage, and user experience; the library focuses on turning images into structured OCR results while preserving a raw-pixel native boundary.

## Why use light-ocr

- **Local by default.** Recognition performs no runtime network access and does not start a child process.
- **Ready for real application pipelines.** It accepts `GRAY8`, `RGB8`, `BGR8`, and `RGBA8` pixel buffers; the Node.js adapter can also decode JPEG and PNG bytes already held in memory.
- **Two deliberate large-image modes.** Bounded/960 remains the fast, memory-conscious default. Opt-in tiled detection preserves more detail for small text and dense 2048-pixel documents while processing one detection tile at a time.
- **Native Apple acceleration when requested.** The `0.2.1` source candidate can route FP16 detection and recognition through Core ML without changing the default CPU behavior or the public OCR result contract.
- **A pinned, reproducible model.** The approximately 31 MB PP-OCRv6 Small bundle is integrity-checked and designed to ship with the application instead of downloading on first use.
- **Consistent across supported platforms.** The same model and result contract are used on macOS, Linux, and Windows.
- **Built for asynchronous hosts.** The Node-API adapter keeps inference away from the JavaScript thread, with bounded queues, cancellation, and explicit lifecycle control.
- **Open and inspectable.** The project is Apache-2.0 licensed and tests real model behavior, high-resolution memory use, lifecycle safety, and output parity in CI.

## Why PP-OCRv6 Small

![Official PP-OCRv6 model and VLM accuracy comparison](docs/assets/ppocrv6-model-comparison.png)

The npm package uses **PP-OCRv6 Small**. On PaddleOCR's in-house multi-scenario benchmark it reports **84.1 detection Hmean** and **81.3 weighted recognition accuracy**, while keeping the model practical for local applications. This chart and these accuracy scores come from the [official PP-OCRv6 evaluation](https://github.com/PaddlePaddle/PaddleOCR/blob/211989f046cc1878460f9e65574690c00a127a1a/docs/version3.x/algorithm/PP-OCRv6/PP-OCRv6.en.md); they are upstream quality results, not latency measurements made by this repository.

## What results look like

For each detected line, light-ocr returns the recognized text, a confidence score, and its position in the original image:

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

Coordinates are quadrilaterals rather than axis-aligned rectangles, so rotated and perspective text can be represented without discarding geometry.

## Measured speed

![The 800 by 180 HELLO 123 benchmark input](docs/assets/benchmark-generated-hello-123.png)

### CPU baseline

For the exact `800×180` BGR input above, light-ocr recognized `HELLO 123` with confidence `0.9893`. The native C++ Release benchmark was run on an Apple M4 Max (16-core CPU, 128 GB RAM), macOS 26.5.1, ONNX Runtime CPU with one intra-op and one inter-op thread, using the default bounded/960 strategy and recognition batch size 1.

| Measurement | Result |
| --- | ---: |
| Warm end-to-end median | **75.678 ms/image** (~13.2 images/s) |
| Warm end-to-end P95 | **79.788 ms/image** |
| Detection + recognition inference median | **74.125 ms/image** |
| Model bundle load, once | 167.906 ms |
| Engine initialization, once | 30.511 ms |

The test uses 5 warm-up runs followed by 30 measured runs. It is a small, synthetic, single-line fixture; latency varies with hardware, input dimensions, text density, and line count. The benchmark contract and comparison with the pinned Python oracle are recorded in [Implementation status](docs/implementation-status.md#本机最终验证快照).

### Apple Core ML acceleration

The `0.2.1` provider gate compared the opt-in FP16 Core ML path with the `cpu_fast` profile on one Apple M4 Max (16-core CPU, 128 GB RAM) running macOS 26.5.1. The CPU profile used up to 12 intra-op threads; each workload used 5 warm-up runs and 3 independent sets of 30 measured runs. The CPU-time reduction describes host process usage, not energy consumption.

| Locked workload | CPU warm P50 | Apple warm P50 | End-to-end speedup | OCR process CPU-time reduction |
| --- | ---: | ---: | ---: | ---: |
| Synthetic `HELLO 123`, 800×180 | 19.774 ms | **8.599 ms** | **2.300×** | **95.91%** |
| Dense XFUND form, 113 text lines | 943.627 ms | **331.011 ms** | **2.851×** | **97.67%** |

The accelerated output also passed all 14 locked quality fixtures: 99.6484% character similarity to the CPU oracle, 100% detection recall, 99.5508% mean matched IoU, 0.004349 mean matched confidence difference, and zero critical failures. These are parity measurements against the CPU output, not independent ground-truth accuracy; FP16 output is not byte-for-byte identical.

The formal warm performance runs peaked at 692.14 MiB RSS and the self-contained Apple model payload added 25.42 MiB. The separate same-engine 100-dense-page lifecycle run peaked at 888.11 MiB and finished 27.47 MiB below its post-warm-up baseline, showing no sustained growth in that run. First use performs offline compilation and loads recognition functions on demand: the fixed `HELLO 123` startup canary took 7.219 s on a compiled-cache miss and 1.275/1.278 s on hits; the 113-line form took 53.846 s on its first full-page miss and 12.677/12.677 s on hits. No provider, compiler, or model is downloaded at runtime.

Only that single M4 Max runner has real-device performance data. The evidence contract classifies it under the `Apple M4` device family for `deviceValidated`; this does not represent separate measurements of every M4 SKU. The candidate's compatibility policy is intentionally open and experimental on other macOS 15+ hardware: M1–M3 and later Apple Silicon can try the same ANE/GPU route, while Intel Macs use Core ML CPU+GPU. Hardware without reviewed evidence reports `deviceValidated: false`; no speedup is claimed until that family has its own data. See the [Apple acceleration design and evidence](docs/apple-device-acceleration.md) for methodology, model placement, quality thresholds, cache behavior, and lifecycle results.

## Get started

### Node.js

Node.js 22 and 24 are supported on macOS arm64/x64, Linux x64 glibc, and Windows x64:

```bash
npm install @arcships/light-ocr
```

The package installs the matching native runtime and the pinned PP-OCRv6 Small model. It does not download a model at first run or compile native code during `postinstall`. Version 0.2.0 supports both `recognizeEncoded()` and raw-pixel `recognize()`.

```ts
import { createEngine } from "@arcships/light-ocr";
import { readFile } from "node:fs/promises";

const engine = await createEngine();
const result = await engine.recognizeEncoded(
  await readFile("image.jpg"),
);

// Raw pixels remain available when the host already owns a decode pipeline.
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

The Apple provider remains opt-in. The following is a maintainer/source-checkout preview, not an `npm install` path for `0.2.0`. It assumes the locked self-contained Apple bundle has already been derived by the local release tooling described in [Build and release](docs/build-and-release.md#8-ci). Request the provider explicitly and keep the whole-session CPU fallback visible:

```ts
const engine = await createEngine({
  // Source candidate only; the planned npm 0.2.1 package will resolve it.
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

`cpuPartition: "allow"` works on both Apple Silicon and Intel Macs. The strict GPU-only profile is Apple-Silicon-only. Normal npm users should keep the default CPU provider until the Apple payload is published.

See the [Node.js guide](bindings/node/README.md) for the full API, cancellation, queue limits, and lifecycle behavior.

### C++ core

Requirements: Python 3 for bootstrap tooling, CMake, and a C++17 compiler. Dependencies and model inputs are pinned; the built runtime does not depend on Python.

```bash
python3 tools/bootstrap_dependencies.py --cache-dir .cache/dependencies
python3 tools/bootstrap_models.py --cache-dir .cache/models

cmake --preset release \
  -DLIGHT_OCR_DEPENDENCY_CACHE_DIR="$PWD/.cache/dependencies"
cmake --build --preset release --parallel
ctest --preset release
```

See [Build and release](docs/build-and-release.md) for platform prerequisites and [C++ API](docs/native-api.md) for integration.

## Package support

| Distribution | Status | Platforms |
| --- | --- | --- |
| C++ core source | Available | macOS arm64/x64, Linux x64 glibc, Windows x64 |
| Node-API adapter source | Available | Node.js 22 and 24 |
| [`@arcships/light-ocr`](https://www.npmjs.com/package/@arcships/light-ocr) | `0.2.0` published | Node.js 22/24 on all Tier 1 platforms |
| [`@arcships/light-ocr-model-ppocrv6-small`](https://www.npmjs.com/package/@arcships/light-ocr-model-ppocrv6-small) | `0.2.0` published | Platform-independent required model dependency |
| Platform native npm packages | `0.2.0` published | macOS arm64/x64, Linux x64 glibc, Windows x64 |

The npm distribution installs one facade, one required model package, and the native package matching the host platform. Package contents, versioning, and release gates are documented in [npm packaging](docs/npm-packaging.md); immutable `0.2.0` hashes and validation evidence are recorded in the [release record](docs/releases/npm-0.2.0.md).

Direct Core ML acceleration is merged on `main` for the `0.2.1` candidate but is not part of the published `0.2.0` package set. Its release keeps the same six-package installation shape; no extra provider package or runtime download is planned.

## Project status

`light-ocr` is under active development. Version `0.2.0` publishes the deterministic `tiled-v1` high-resolution mode and bounded in-memory JPEG/PNG decoding in the Node.js adapter without changing the raw-pixel C++ Core boundary. The current `0.2.1` source candidate adds opt-in Direct Core ML execution on macOS while preserving CPU as the default.

As a pre-1.0 project, public APIs and package layout may still evolve; the project does not currently promise a stable cross-release C++ ABI.

The Core CI builds and tests the project on:

- macOS arm64
- macOS x64
- Linux x64 with glibc
- Windows x64

It also runs sanitizers, fuzz smoke tests, offline-runtime checks, output parity, quality, and memory gates. Performance qualification is a separate, explicitly triggered workflow and is not part of ordinary CI or release preflight. See the [current implementation status](docs/implementation-status.md) for verified results and known gaps.

## Documentation

- [Changelog](CHANGELOG.md)
- [C++ API](docs/native-api.md)
- [Node.js adapter](bindings/node/README.md)
- [Build and release](docs/build-and-release.md)
- [Model bundle](docs/model-bundle.md)
- [Accuracy and parity](docs/parity-testing.md)
- [High-resolution memory behavior](docs/memory-optimization.md)
- [Architecture](docs/architecture.md)
- [Implementation status](docs/implementation-status.md)

## Community

Issues and pull requests are welcome. If you are considering light-ocr for a product, feel free to [open an issue](https://github.com/arcships/light-ocr/issues) describing the platform, image source, language mix, and expected workload. Real application scenarios help shape package priorities and future model support.

When reporting a bug, please include the platform, input dimensions and pixel format, the model bundle ID, and a minimal reproduction when possible. Do not attach private source images unless you are comfortable publishing them.

## License

light-ocr is available under the [Apache License 2.0](LICENSE). Third-party dependencies and model notices are included with their corresponding release artifacts.
