# light-ocr: Local OCR Skill

Use this skill when you need to extract text from local images (PNG, JPEG) with precise coordinates, confidence scores, and stable error handling — without writing Node.js integration code or relying on a multimodal model's ability to read small text.

## When to use OCR instead of a multimodal model

- Small text, dense text, or text in screenshots/labels/receipts/forms that a multimodal model misreads or hallucinates.
- When you need exact text + bounding box coordinates (for field extraction, redaction, counting, or downstream layout analysis).
- When you need deterministic, offline, reproducible results (no network, no API calls).

## Commands

```bash
# Full OCR: recognize text + coordinates (default action)
light-ocr image.png --format json
light-ocr image.png --format text              # just text, no coordinates

# Region-only recognition (ROI)
light-ocr recognize image.png --region 100,80,640,320 --format json

# Detect-only: just text region boxes, no recognition
light-ocr detect image.png                     # output is always JSON
light-ocr detect image.png --crop              # include PNG crop per box

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

All `recognize`/`detect` output uses `--schema-version 1` (default). The envelope:

```json
{
  "schemaVersion": 1,
  "source": { "kind": "image", "mediaType": "...", "identity": {}, "appliedTransforms": {} },
  "pages": [{ "index": 0, "width": ..., "height": ..., "coordinateSpace": "pageSpace", "structure": "ocr-order|detect", "lines|detections": [] }]
}
```

- `recognize`: `pages[0].lines[]` with `{ id: "L0", text, confidence, box: [4 points] }`
- `detect`: `pages[0].detections[]` with `{ id: "D0", score, box: [4 points] }`
- `--format text`: only recognized text, one line per line (no coordinates)
- `--format jsonl`: one page record per line (for streaming/batch)

## Choosing what to run

| You want | Command |
| --- | --- |
| Full text from an image | `recognize --format text` |
| Text + coordinates | `recognize --format json` |
| Just where text is (no text) | `detect` |
| Text in a specific area | `recognize --region x,y,w,h` |
| Engine/provider info | `info --model-info` |
| Quick version check | `info --version` |

## Exit codes

| Code | Meaning | Agent action |
| --- | --- | --- |
| 0 | Success | Parse stdout |
| 64 | Usage error | Fix command syntax |
| 65 | Invalid argument (bad region, unsupported format/schema) | Fix input |
| 66 | Invalid image | Try different image |
| 67 | Unsupported capability | Check `info --model-info` |
| 68 | Model/bundle error | Reinstall package |
| 69 | Resource limit exceeded | Smaller image or region |
| 70 | Environment/package failure | Check native addon |
| 71 | Inference failure | Retry or report bug |
| 72 | Internal error | Report bug |

## How to handle failures

- **Empty result**: No text found. The image may have no text, or text is too small/low-contrast. Try `--region` on specific areas, or check with `detect` first.
- **Low confidence**: Lines with `confidence < 0.5` may be unreliable. Don't present inferred or guessed text as OCR output — always cite the actual `text` field and its `confidence`.
- **Resource limit (exit 69)**: Image too large. Use `--region` to process a sub-area, or downscale before passing.
- **Unsupported capability (exit 67)**: The requested provider or feature isn't available. Run `info --model-info` to see what's supported.

## Important rules

1. **Never fabricate OCR text.** Only use text from the `text` field of the result. If confidence is low, say so — don't guess.
2. **Cite coordinates when relevant.** Box coordinates are in `pageSpace` (top-left origin, x right, y down, post-EXIF pixels). Use them for field extraction, redaction, or counting.
3. **Use `--schema-version 1`** for reproducible output. Don't parse help text programmatically.
4. **Prefer `detect` first** if you only need to locate text regions (faster, no recognition).
5. **Use `--region`** to avoid processing huge images unnecessarily — detect first, then recognize specific regions.
6. **Check exit codes** before parsing stdout. Non-zero exit means stdout may be empty; stderr has the error.

## Validation script

```bash
# Quick smoke test: recognize a known image and check exit code
light-ocr test-image.png --format text && echo "OK: $(light-ocr test-image.png --format text | wc -l) lines"
```

## Related

- [CLI design](docs/cli-design.md) — full flag reference, coordinate semantics, exit codes
- [Roadmap N1](docs/roadmap.md) — product context and acceptance criteria
