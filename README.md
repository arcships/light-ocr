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

Recognize text in PDF, JPEG, PNG, or raw image data directly on your machine.
`light-ocr` returns lines in reading order with confidence scores and
quadrilateral coordinates. The Node.js package includes PP-OCRv6 Small,
PDFium, a Chinese fallback font, and prebuilt components for macOS, Linux, and
Windows—without postinstall or first-run downloads.

| | |
| --- | --- |
| **Best for** | local OCR in Node.js apps, CLIs, desktop software, and native C++ integrations |
| **Inputs** | JPEG, PNG, PDF, encoded bytes, or decoded pixel buffers |
| **Outputs** | text, confidence, quadrilateral boxes, page metadata, and timing |
| **Distribution** | one npm install; CommonJS, ESM, TypeScript, and six prebuilt platforms |

## Quick start

Node.js 22 and 24 are supported.

```bash
npm install @arcships/light-ocr
```

```ts
import { createEngine } from "@arcships/light-ocr";
import { readFile } from "node:fs/promises";

const engine = await createEngine();

try {
  const result = await engine.recognizeEncoded(
    await readFile("image.jpg"),
  );

  for (const line of result.lines) {
    console.log(line.text, line.confidence, line.box);
  }
} finally {
  await engine.close();
}
```

`createEngine()` automatically chooses the right execution mode for the
current platform. If your application already decodes images,
[`recognize()`](packages/light-ocr/README.md#recognize-decoded-pixels) also
accepts `GRAY8`, `RGB8`, `BGR8`, and `RGBA8` pixel data.

PDF pages use the same package:

```ts
import { recognizeDocument } from "@arcships/light-ocr";

for await (const page of recognizeDocument("report.pdf", { dpi: 200 })) {
  console.log(page.index, page.lines.map((line) => line.text));
}
```

CommonJS uses the same exports through `require("@arcships/light-ocr")`.

## CLI

The `light-ocr` command is available after install — no extra setup:

```bash
# Recognize text + coordinates
light-ocr image.png --format json

# Just text
light-ocr image.png --format text

# PDF pages, using the renderer already included by npm
light-ocr report.pdf --pages 1-10 --format text

# Detect text regions only (no recognition)
light-ocr detect image.png

# Region of interest
light-ocr recognize image.png --region 100,80,640,320 --format json

# Engine info
light-ocr info --version

# System diagnostics (hardware and providers)
light-ocr doctor --json
```

Image commands are `recognize` (default), `detect` (boxes only), `info`
(version diagnostics), and `doctor` (system diagnostics). A `.pdf` path routes
directly to document OCR; `document` handles explicit multi-source jobs. Output
uses a versioned `schemaVersion: 1` contract. EXIF orientation is corrected
automatically. See the [CLI design](docs/cli-design.md) and
[npm package README](packages/light-ocr/README.md) for the complete
install-and-use reference.

## PDF and multi-page documents

PDF and multi-page OCR are built into `@arcships/light-ocr`. The matching
PDFium binary and checksum-pinned Noto Sans SC fallback font are carried by
the same platform npm package as the OCR runtime. This keeps PDFs that reference
common non-embedded Chinese fonts renderable before OCR, with no postinstall
script, runtime download, compiler, system-font requirement, or separate
package to install.

```bash
# Single PDF with default 150 DPI
light-ocr report.pdf

# Page range with streaming JSONL output
light-ocr report.pdf --pages 1-10 --format jsonl

# Multiple images as one document
light-ocr document scan1.png scan2.png scan3.png --format text
```

Programmatic API:

```ts
import { recognizeDocument } from "@arcships/light-ocr";

// Stream pages from a PDF
for await (const page of recognizeDocument("report.pdf", { dpi: 200 })) {
  console.log(page.index, page.lines.length, page.source.kind);
}

// Multiple images
for await (const page of recognizeDocument([buf1, buf2, buf3])) {
  console.log(page.index, page.lines);
}
```

## What you get

- **Local processing.** Images, PDFs, and OCR results stay on your machine.
- **One package to install.** The model, OCR runtime, PDF renderer, and Chinese fallback font are included through the npm package's platform dependency.
- **No secondary downloads.** Installation and runtime need no postinstall fetch, compiler, model download, PDF engine download, or font download.
- **Useful output.** Every line includes recognized text, confidence, and its position in the original image.
- **Hardware acceleration by default.** Auto tries Core ML first on macOS 15+ Apple Silicon, and WebGPU first on the Linux and Windows builds below.
- **Application-friendly execution.** Recognition runs off the JavaScript main thread and supports queues, cancellation, and explicit cleanup.
- **Small text in large images.** An optional `tiled` mode preserves small and dense text in high-resolution images.

> **macOS re-signing.** Downstream macOS packagers may re-sign the native binaries with their own Developer ID (or ad-hoc) identity, for example when notarizing a distributed app. On macOS the loader accepts a re-signed Mach-O when its code signature verifies and its signing identity matches the host application (same TeamIdentifier, or both ad-hoc signed); other platforms and unsigned mutations keep the strict size + SHA-256 gate.

> ⭐ **Like light-ocr?** Give it a star — it helps others discover the project and keeps us motivated!

## Platform acceleration

The npm package provides the following six builds. The default `createEngine()` call uses Auto mode:

| Platform | Auto mode |
| --- | --- |
| macOS on Apple Silicon | Core ML on macOS 15+, then CPU |
| macOS on Intel | CPU |
| Linux x64 with glibc | WebGPU through Vulkan, then CPU |
| Linux arm64 with glibc | CPU |
| Windows x64 | WebGPU through D3D12, then CPU |
| Windows arm64 | CPU |

Applications that need explicit control can choose `auto`, `cpu`, `apple`, or
`webgpu` through the
[`execution` option](packages/light-ocr/README.md#platform-acceleration).

## Model tiers

Small remains the stable default. Tiny and Medium are opt-in preview packages
under the `next` tag. All three expose the same API, types, result schema, and
error model, while each install contains only its selected model.

| Tier | Package / command | Model payload | Status |
| --- | --- | ---: | --- |
| Small | `@arcships/light-ocr` / `light-ocr` | ~30 MB | stable default |
| Tiny | `@arcships/light-ocr-tiny@next` / `light-ocr-tiny` | ~6.3 MB | preview; 49 languages, no Japanese |
| Medium | `@arcships/light-ocr-medium@next` / `light-ocr-medium` | ~139 MB | preview; quality-first |

Tiny and Medium stay on `next`; they do not change what
`npm install @arcships/light-ocr` installs.

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

## Agent Skill

An [Agent Skill](.agents/skills/local-ocr/SKILL.md) is included for AI agents
that can call local commands. It provides scenario-driven workflows, command
selection guidance, and exit code reference:

- When to use OCR vs. a multimodal model
- Detect-then-recognize workflows for large images
- ROI field extraction for receipts and forms
- Verifying multimodal output against deterministic OCR

## Documentation

- [npm package README](packages/light-ocr/README.md)
- [CLI reference](docs/cli-design.md)
- [Node.js image engine and CLI details](bindings/node/README.md)
- [Agent Skill](.agents/skills/local-ocr/SKILL.md)
- [C++ API](docs/native-api.md)
- [Apple Silicon acceleration](docs/apple-device-acceleration.md)
- [Linux WebGPU acceleration](docs/linux-device-acceleration.md)
- [Windows WebGPU acceleration](docs/windows-device-acceleration.md)
- [Model bundle](docs/model-bundle.md)
- [Build and release](docs/build-and-release.md)
- [Roadmap](docs/roadmap.md)
- [Changelog](CHANGELOG.md)
- [npm 0.5.6 release preparation — English](docs/releases/npm-0.5.6.en.md)
- [npm 0.5.6 发布准备记录 — 中文](docs/releases/npm-0.5.6.md)
- [npm 0.3.0 release report](docs/releases/npm-0.3.0.md)

## Community and license

Issues and pull requests are welcome — see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines. All participants are expected to follow our [Code of Conduct](CODE_OF_CONDUCT.md).

`light-ocr` is available under the [Apache License 2.0](LICENSE).
