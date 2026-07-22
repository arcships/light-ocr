# light-ocr

Local OCR for Node.js and Agents. Offline, no Python, no network calls. Returns text with coordinates and confidence.

## Install

```bash
npm install @arcships/light-ocr
```

The install includes the PP-OCRv6 Small model and a prebuilt native runtime for your platform. No post-install scripts, no downloads — `createEngine()` works out of the box.

## CLI

The `light-ocr` command is available after install.

### Quick start

```bash
# Recognize text in an image (default action)
light-ocr image.png --format json

# Just text, no coordinates
light-ocr image.png --format text

# Engine info
light-ocr info --version
light-ocr info --model-info
```

### Subcommands

| Command | Purpose |
| --- | --- |
| `recognize <path> [flags]` | Full OCR: detection + recognition (default) |
| `detect <path> [flags]` | Detection only: text region boxes, no recognition |
| `info --model-info \| --version` | Engine/version diagnostics, no image read |

`light-ocr image.png` without a subcommand is implicit `recognize`.

### Common flags

| Flag | Values | Description |
| --- | --- | --- |
| `--format` | `json` \| `jsonl` \| `text` | Output format (default: `json`) |
| `--region` | `x,y,w,h` | Restrict recognition to a pageSpace rectangle |
| `--provider` | `auto` \| `cpu` \| `apple` \| `webgpu` | Execution provider (default: `auto`) |
| `--no-exif` | — | Disable EXIF orientation correction |
| `--schema-version` | `1` | Request exact output schema |
| `--quiet` | — | Suppress non-error stderr |

`detect` does not accept `--format` (output is always JSON with `detections[]`). `detect --crop` attaches a PNG crop per detection.

### Scenarios

**Screenshot with small text:**
```bash
light-ocr screenshot.png --format json
# Low confidence? Re-run on a specific region:
light-ocr recognize screenshot.png --region 100,80,640,320 --format json
```

**Form/receipt field extraction (detect-then-recognize):**
```bash
light-ocr detect receipt.png                    # find where text is
light-ocr recognize receipt.png --region 50,200,300,80 --format json  # read that area
```

**stdin / pipe:**
```bash
cat image.png | light-ocr recognize --stdin --type image/png --format json
```

**Batch via shell:**
```bash
for f in *.png; do light-ocr recognize "$f" --format jsonl; done
```

### Output schema

```json
{
  "schemaVersion": 1,
  "source": { "kind": "image", "mediaType": "image/png", "identity": {}, "appliedTransforms": {} },
  "pages": [{
    "index": 0,
    "width": 640,
    "height": 480,
    "coordinateSpace": "pageSpace",
    "structure": "ocr-order",
    "lines": [{ "id": "L0", "text": "HELLO", "confidence": 0.99, "box": [{"x":0,"y":0},{"x":100,"y":0},{"x":100,"y":30},{"x":0,"y":30}] }]
  }]
}
```

- `box` is 4 points in `pageSpace` (top-left origin, x right, y down, post-EXIF pixels)
- `detect` replaces `lines` with `detections[]` (`{ id, score, box }`) and sets `structure: "detect"`
- `--format text`: recognized text only, one line per line
- `--format jsonl`: one page record per line (for streaming/batch)

### Exit codes

| Code | Meaning |
| --- | --- |
| 0 | Success |
| 64 | Usage error |
| 65 | Invalid argument (bad region, unsupported format/schema) |
| 66 | Invalid image |
| 67 | Unsupported capability |
| 68 | Model/bundle error |
| 69 | Resource limit exceeded |
| 70 | Environment/package failure |
| 71 | Inference failure |
| 72 | Internal error |

## Node API

```js
const { createEngine } = require('@arcships/light-ocr');

const engine = await createEngine();

const result = await engine.recognizeEncoded(imageBytes);
console.log(result.lines[0].text);        // "HELLO"
console.log(result.lines[0].confidence);   // 0.99
console.log(result.lines[0].box);           // [{x,y}, {x,y}, {x,y}, {x,y}]

await engine.close();
```

### Detect (detection only, no recognition)

```js
const engine = await createEngine();
const result = await engine.detect(imageBytes);
console.log(result.boxes[0].score);  // 0.95
console.log(result.boxes[0].box);    // [{x,y}, {x,y}, {x,y}, {x,y}]
await engine.close();
```

### With options

```js
const engine = await createEngine({
  execution: { provider: 'auto' },
  queueCapacity: 4,
});

const result = await engine.recognizeEncoded(imageBytes, {
  applyExif: true,       // EXIF orientation correction (default: true)
  region: { x: 100, y: 80, width: 640, height: 320 },  // ROI
  signal: controller.signal,
});

console.log(engine.info.execution.sessions.detection.actualProviderChain);
await engine.close();
```

### ESM

```js
import { createEngine } from '@arcships/light-ocr';
```

TypeScript types are included in [`index.d.ts`](js/index.d.ts).

## Capabilities

- **Formats**: JPEG, PNG (memory input via `Uint8Array`)
- **EXIF orientation**: JPEG orientation tags 1–8 automatically corrected
- **ROI**: pageSpace axis-aligned rectangle, coordinates offset back to full page
- **Providers**: CPU (all platforms), Apple Core ML (macOS arm64), WebGPU (Linux x64, Windows x64)
- **Offline**: no network at install or runtime; model bundled in npm package
- **Concurrency**: one engine = one worker thread, bounded FIFO queue, AbortSignal support
- **Platforms**: macOS arm64/x64, Linux x64/arm64 (glibc), Windows x64/arm64
- **Node.js**: 22, 24

### Not supported

- WebP, GIF, PDF, TIFF
- Character-level coordinates (recognition is line-level)
- Multi-engine fan-out as default architecture
- Bun (Node-API lifecycle not fully verified)

## Acceleration

`provider: 'auto'` (default) selects the best available backend:

| Platform | Auto candidate | Notes |
| --- | --- | --- |
| macOS arm64 | Apple Core ML → CPU | FP16 detection + recognition via Neural Engine / GPU |
| macOS x64 | CPU | Core ML parity not met on Intel |
| Linux x64 | WebGPU → CPU | FP32, bounded CPU partition for Concat/Gather/Slice |
| Windows x64 | WebGPU → CPU | FP32, D3D12 |
| Linux arm64 | CPU | WebGPU plugin has no arm64 binary |
| Windows arm64 | CPU | WebGPU not yet qualified |

Use `light-ocr info --model-info` to inspect the actual provider chain, device, and qualification status.

## Build from source

See [CONTRIBUTING.md](../../CONTRIBUTING.md) for prerequisites and the full build guide.

```bash
# Clone and initialize
git clone https://github.com/arcships/light-ocr.git
cd light-ocr

# Build C++ core
cmake -S . -B build-ci -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DLIGHT_OCR_DEPENDENCY_CACHE_DIR="$PWD/.cache/dependencies"
cmake --build build-ci --parallel
ctest --test-dir build-ci --output-on-failure

# Build Node addon
cd bindings/node
NODE_INCLUDE_DIR="$(node -p "require('node:path').resolve(require('node:path').dirname(process.execPath), '../include/node')")"

cmake -S ../.. -B build-node -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DLIGHT_OCR_DEPENDENCY_CACHE_DIR="$PWD/../../.cache/dependencies" \
  -DLIGHT_OCR_BUILD_NODE=ON -DLIGHT_OCR_BUILD_TESTS=ON -DLIGHT_OCR_BUILD_TOOLS=OFF \
  -DLIGHT_OCR_NODE_INCLUDE_DIR="$NODE_INCLUDE_DIR" \
  -DLIGHT_OCR_NODE_EXECUTABLE="$(command -v node)"
cmake --build build-node --target light_ocr_node --parallel
```

Windows builds additionally require `LIGHT_OCR_NODE_LIBRARY` pointing to `node.lib`. The build links a delay-load hook so the addon works under Electron as well as Node.

Run tests:

```bash
node --test --test-concurrency=1 bindings/node/test/*.test.cjs
```

## License

Apache-2.0. The bundled PP-OCRv6 model inherits PaddleOCR's license terms.
