# @arcships/light-ocr

[![npm version](https://img.shields.io/npm/v/%40arcships%2Flight-ocr?color=CB3837)](https://www.npmjs.com/package/@arcships/light-ocr)
[![Node.js 22 and 24](https://img.shields.io/badge/Node.js-22%20%7C%2024-339933)](https://nodejs.org/)
[![Apache-2.0](https://img.shields.io/npm/l/%40arcships%2Flight-ocr)](https://github.com/arcships/light-ocr/blob/main/LICENSE)

![light-ocr](https://raw.githubusercontent.com/arcships/light-ocr/main/docs/assets/light-ocr-banner.png)

**Offline image and PDF OCR for Node.js — prebuilt, typed, and
self-contained.**

`@arcships/light-ocr` is the stable PP-OCRv6 Small package. It recognizes
JPEG, PNG, PDF, decoded pixels, and multi-page image jobs without Python,
cloud APIs, postinstall downloads, or local native compilation.

| | |
| --- | --- |
| **Input** | JPEG, PNG, PDF, `Uint8Array`, or decoded pixel buffers |
| **Output** | text lines in reading order, confidence, quadrilateral boxes, and timing |
| **Runtime** | CommonJS + ESM + bundled TypeScript declarations |
| **Platforms** | macOS, Linux glibc, and Windows on x64 and ARM64 |
| **Node.js** | 22 and 24 |

## Install

```bash
npm install @arcships/light-ocr
```

That single command installs the Small model, shared JavaScript runtime, and
the native OCR/PDF package matching the current platform. The package has no
install script and remains installable with scripts disabled.

## Quick start

### Recognize an image

```js
import { createEngine } from "@arcships/light-ocr";
import { readFile } from "node:fs/promises";

const engine = await createEngine();

try {
  const result = await engine.recognizeEncoded(
    await readFile("receipt.png"),
  );

  for (const line of result.lines) {
    console.log(line.text, line.confidence, line.box);
  }
} finally {
  await engine.close();
}
```

CommonJS uses the same API:

```js
const { createEngine } = require("@arcships/light-ocr");
```

### Stream pages from a PDF

```js
import { recognizeDocument } from "@arcships/light-ocr";

for await (const page of recognizeDocument("report.pdf", {
  dpi: 200,
  pageRange: { start: 1, end: 10 },
})) {
  console.log(`page ${page.index + 1}`);
  for (const line of page.lines) console.log(line.text);
}
```

`recognizeDocument()` accepts a file path, PDF/image bytes, or an array of
image paths and byte buffers. Pages are yielded as they finish, so callers do
not need to retain the complete document result.

### Use the CLI

The `light-ocr` command is included:

```bash
# Image OCR
light-ocr image.png --format text

# PDF OCR; PDFium and the fallback font are already installed
light-ocr report.pdf --pages 1-10 --format jsonl

# Multiple images as one document
light-ocr document scan-1.png scan-2.png scan-3.png --format text

# Detection only
light-ocr detect screenshot.png

# Voluntary hardware/provider diagnostics
light-ocr doctor --json
```

## Everything needed for PDF OCR is included

The package does not defer essential files to an installer or first-run
download:

| Component | Distribution |
| --- | --- |
| PP-OCRv6 Small model | exact-pinned required npm dependency |
| JavaScript runtime and types | exact-pinned required npm dependency |
| Native OCR addon and runtime libraries | matching platform npm dependency |
| PDFium addon and shared library | inside the matching platform package |
| Noto Sans SC fallback font and OFL license | inside the matching platform package |

The bundled, checksum-pinned fallback font is used when a PDF references a
common Chinese font without embedding its glyphs. PDFium therefore renders the
page correctly before OCR instead of handing missing-glyph boxes to the model.

There is no postinstall fetch, runtime font/model/PDFium download, GitHub
access, compiler requirement, or system Chinese-font requirement on customer
machines.

## Node.js API

### Main exports

| Export | Purpose |
| --- | --- |
| `createEngine(options?)` | Create a reusable image OCR engine |
| `recognizeDocument(source, options?)` | Stream PDF or image pages with automatic cleanup |
| `createDocumentEngine(options?)` | Reuse one engine across multiple document jobs |
| `hasPdfSupport()` | Check that the bundled PDF renderer can be loaded |
| `modelProfile` | Inspect the selected model tier and language metadata |
| `OcrError` | Stable typed error with a machine-readable `code` |

### Recognize decoded pixels

Applications that already decode images can avoid re-encoding:

```js
const result = await engine.recognize({
  data: rgbaBytes,
  width,
  height,
  stride: width * 4,
  pixelFormat: "rgba8",
});
```

Supported pixel formats are `gray8`, `rgb8`, `bgr8`, and `rgba8`.

### Region, cancellation, and execution provider

```js
const controller = new AbortController();

const engine = await createEngine({
  execution: { provider: "auto" },
  queueCapacity: 4,
});

try {
  const result = await engine.recognizeEncoded(imageBytes, {
    region: { x: 100, y: 80, width: 640, height: 320 },
    applyExif: true,
    signal: controller.signal,
  });

  console.log(result.lines);
  console.log(engine.info.execution.selectionTrace);
} finally {
  await engine.close();
}
```

Recognition runs on a dedicated worker instead of blocking the JavaScript main
thread. Queues are bounded, `AbortSignal` is supported, and engines must be
closed explicitly when the application is finished with them.

### Document resource limits

PDF and multi-page calls apply conservative defaults before or during
rendering:

| Option | Default |
| --- | ---: |
| `dpi` | 150 |
| `maxFileBytes` | 100 MiB per input |
| `maxPages` | 100 |
| `maxPagePixels` | 4096 × 4096 |
| `maxTotalPixels` | 100 Mi pixels |

Limits are configurable per call. Violations reject with
`OcrError.code === "resource_limit_exceeded"`.

### Result shape

Image OCR returns one line entry per recognized line:

```json
{
  "text": "TOTAL 42.00",
  "confidence": 0.98,
  "box": [
    { "x": 24, "y": 80 },
    { "x": 190, "y": 80 },
    { "x": 190, "y": 108 },
    { "x": 24, "y": 108 }
  ]
}
```

Coordinates use `pageSpace`: top-left origin, positive x to the right, and
positive y downward after EXIF correction. Document pages add a stable page
index, source metadata, dimensions, applied PDF transforms, and timing.

## CLI reference

| Command | Purpose |
| --- | --- |
| `light-ocr [recognize] <image>` | Full image OCR; `recognize` is optional |
| `light-ocr <document.pdf>` | Stream a PDF through document OCR |
| `light-ocr document <source...>` | Process a PDF or multiple page images |
| `light-ocr detect <image>` | Return text-region boxes without recognition |
| `light-ocr info --version` | Print package/Core version information |
| `light-ocr info --model-info` | Print model and execution information |
| `light-ocr doctor --json` | Print voluntary system/provider diagnostics |

Image OCR supports `--format json|jsonl|text`, `--region x,y,w,h`,
`--provider auto|cpu|apple|webgpu`, `--stdin`, and automatic EXIF correction.
Document OCR adds `--pages N-M`, `--dpi`, and the page/file/pixel limit flags.

`detect` always emits structured JSON and does not accept `--format`.
Diagnostics do not include the hostname or a stable device identifier.

## Platform acceleration

The default `provider: "auto"` chooses a qualified accelerator when available
and ends with CPU as the stable fallback:

| Platform | Auto path |
| --- | --- |
| macOS 15+ on Apple Silicon | Core ML, then CPU |
| macOS on Intel | CPU |
| Linux x64 with glibc | WebGPU through Vulkan, then CPU |
| Linux arm64 with glibc | CPU |
| Windows x64 | WebGPU through D3D12, then CPU |
| Windows arm64 | CPU |

Explicit `cpu`, `apple`, and `webgpu` requests fail closed when the requested
provider is unavailable; they do not silently switch providers.

## Model tiers

This package is the stable Small tier. Tiny and Medium are opt-in previews with
the same API, types, result schema, and error model:

| Tier | Install | Model payload | Status |
| --- | --- | ---: | --- |
| Small | `npm install @arcships/light-ocr` | about 30 MB | stable default |
| Tiny | `npm install @arcships/light-ocr-tiny@next` | about 6.3 MB | preview; 49 languages, no Japanese |
| Medium | `npm install @arcships/light-ocr-medium@next` | about 139 MB | preview; quality-first |

Each facade installs only its selected model.

## Errors and diagnostics

Expected failures reject with `OcrError` and a stable `code`, including
`invalid_argument`, `invalid_image`, `resource_limit_exceeded`,
`package_load_failed`, and `inference_failed`.

For environment reports:

```bash
light-ocr doctor --json
```

For package/PDF capability checks:

```js
import { hasPdfSupport, modelProfile } from "@arcships/light-ocr";

console.log(hasPdfSupport());
console.log(modelProfile);
```

## Documentation

- [Project overview](https://github.com/arcships/light-ocr)
- [Node.js and CLI reference](https://github.com/arcships/light-ocr/blob/main/bindings/node/README.md)
- [PDF 0.5.6 release notes](https://github.com/arcships/light-ocr/blob/main/docs/releases/npm-0.5.6.en.md)
- [Agent Skill](https://github.com/arcships/light-ocr/blob/main/.agents/skills/local-ocr/SKILL.md)
- [Changelog](https://github.com/arcships/light-ocr/blob/main/CHANGELOG.md)
- [Issues](https://github.com/arcships/light-ocr/issues)

## License

Apache-2.0. The package also carries the applicable licenses and notices for
the bundled PP-OCRv6 model, native runtimes, PDFium, and Noto Sans SC font.
