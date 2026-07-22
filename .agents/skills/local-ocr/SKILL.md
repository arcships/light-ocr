---
name: local-ocr
description: Extract text from local images (PNG, JPEG) with precise coordinates, confidence scores, and stable error handling using the light-ocr CLI. Use when needing to read small/dense text in screenshots, receipts, labels, forms, or documents that a multimodal model may misread; when exact text plus bounding box coordinates are needed for field extraction, redaction, counting, or layout analysis; or when deterministic offline OCR is required without network or API calls.
---

# local-ocr

`light-ocr` is a local OCR engine for Node.js. It runs offline, returns text with coordinates and confidence, and follows a strict stdout/stderr contract for scripting.

## Commands

```bash
# Full OCR: text + coordinates (default action)
light-ocr image.png --format json
light-ocr image.png --format text              # text only, no coordinates

# Region-only recognition (ROI in pageSpace pixels)
light-ocr recognize image.png --region 100,80,640,320 --format json

# Detect-only: text region boxes, no recognition (always JSON)
light-ocr detect image.png
light-ocr detect image.png --crop              # attach PNG crop per box

# Diagnostics (no image read)
light-ocr info --model-info                    # full EngineInfo JSON
light-ocr info --version                       # npm/core/model triple

# stdin
cat image.png | light-ocr recognize --stdin --type image/png --format json

# Execution provider
light-ocr recognize image.png --provider auto  # default: auto-select best
light-ocr recognize image.png --provider cpu    # force CPU
```

## Output schema

All `recognize`/`detect` output wraps in a `schemaVersion: 1` envelope:

```json
{
  "schemaVersion": 1,
  "source": { "kind": "image", "mediaType": "...", "identity": {}, "appliedTransforms": {} },
  "pages": [{ "index": 0, "width": ..., "height": ..., "coordinateSpace": "pageSpace", "structure": "ocr-order|detect", "lines|detections": [] }]
}
```

- `recognize`: `pages[0].lines[]` → `{ id: "L0", text, confidence, box: [4 points] }`
- `detect`: `pages[0].detections[]` → `{ id: "D0", score, box: [4 points] }`
- `--format text`: recognized text only, one line per line
- `--format jsonl`: one page record per line (for streaming/batch)

## Choosing what to run

| Goal | Command |
| --- | --- |
| Full text from an image | `recognize --format text` |
| Text + coordinates | `recognize --format json` |
| Just where text is (no text) | `detect` |
| Text in a specific area | `recognize --region x,y,w,h` |
| Engine/provider info | `info --model-info` |
| Quick version check | `info --version` |

## Exit codes

| Code | Meaning | Action |
| --- | --- | --- |
| 0 | Success | Parse stdout |
| 64 | Usage error | Fix command syntax |
| 65 | Invalid argument (bad region, unsupported format/schema) | Fix input |
| 66 | Invalid image | Try different image |
| 67 | Unsupported capability | Check `info --model-info` |
| 68 | Model/bundle error | Reinstall package |
| 69 | Resource limit exceeded | Use smaller image or `--region` |
| 70 | Environment/package failure | Check native addon |
| 71 | Inference failure | Retry or report bug |
| 72 | Internal error | Report bug |

## Failure handling

- **Empty result**: No text found. Try `detect` first to see if any regions were detected, then `recognize --region` on specific areas.
- **Low confidence** (< 0.5): Do not present inferred text as OCR output. Always cite the actual `text` field and `confidence`.
- **Resource limit** (exit 69): Image too large. Use `--region` to process a sub-area.
- **Unsupported capability** (exit 67): Run `info --model-info` to check available providers.

## Rules

1. Never fabricate OCR text. Only use the `text` field from results. If confidence is low, state it.
2. Cite coordinates when relevant. Box coordinates are in `pageSpace` (top-left origin, x right, y down, post-EXIF pixels).
3. Use `--schema-version 1` for reproducible output. Do not parse help text programmatically.
4. Prefer `detect` first if only locating text regions (faster, no recognition).
5. Use `--region` to avoid processing huge images unnecessarily.
6. Check exit codes before parsing stdout. Non-zero exit means stdout may be empty; read stderr for the error.
