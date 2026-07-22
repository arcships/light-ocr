---
name: local-ocr
description: Extract text from local images (PNG, JPEG) with precise coordinates, confidence scores, and stable error handling using the light-ocr CLI. Use when needing to read small/dense text in screenshots, receipts, labels, forms, or documents that a multimodal model may misread; when exact text plus bounding box coordinates are needed for field extraction, redaction, counting, or layout analysis; or when deterministic offline OCR is required without network or API calls.
---

# local-ocr

`light-ocr` is a local OCR engine. It runs offline, returns text with coordinates and confidence, and follows a strict stdout/stderr contract for scripting.

## Scenarios

### Screenshot with small text

A user shares a screenshot and asks about specific text that is too small or dense to read visually.

```bash
# Step 1: recognize the full image
light-ocr screenshot.png --format json
```

If the result has low confidence or missing text in a region:

```bash
# Step 2: re-run on the specific region (coordinates from step 1 boxes)
light-ocr recognize screenshot.png --region 100,80,640,320 --format json
```

### Form or receipt field extraction

Need to extract specific fields (names, amounts, dates) from a form or receipt image.

```bash
# Step 1: detect where text regions are (fast, no recognition)
light-ocr detect receipt.png

# Step 2: recognize only the region containing the target field
light-ocr recognize receipt.png --region 50,200,300,80 --format json
```

This two-step pattern saves time on large images: detect first, then recognize only the regions of interest.

### Counting text regions

Need to count how many text lines or regions exist in an image.

```bash
light-ocr detect image.png | python -c "import json,sys; print(len(json.load(sys.stdin)['pages'][0]['detections']))"
```

### Verifying multimodal model output

A multimodal model claims to read text from an image. Verify the claim against deterministic OCR.

```bash
light-ocr image.png --format text
```

Compare the text output with the model's claim. If they differ, trust the OCR `text` field — do not fabricate.

### Batch processing via shell

Process multiple images sequentially with JSONL output.

```bash
for f in *.png; do
  light-ocr recognize "$f" --format jsonl
done
```

Each line is one page record. Check exit codes: a non-zero exit for one image does not stop the loop, but stdout for that image may be empty.

## Decision flow

```
Need text from an image?
├── Know which region? → recognize --region x,y,w,h --format json
├── Need full text only? → recognize --format text
├── Need text + coordinates? → recognize --format json
├── Only need where text is? → detect
├── Large image, unsure where text is? → detect first, then recognize --region
└── Need engine info or version? → info --model-info / info --version
```

## Output schema

```json
{
  "schemaVersion": 1,
  "source": { "kind": "image", "mediaType": "...", "identity": {}, "appliedTransforms": {} },
  "pages": [{
    "index": 0,
    "width": 640, "height": 480,
    "coordinateSpace": "pageSpace",
    "structure": "ocr-order",
    "lines": [{ "id": "L0", "text": "HELLO", "confidence": 0.99, "box": [4 points] }]
  }]
}
```

- `box` is 4 points in `pageSpace` (top-left origin, x right, y down, post-EXIF pixels)
- `detect` replaces `lines` with `detections[]` (`{ id, score, box }`) and sets `structure: "detect"`
- `--format text`: recognized text only, one line per line, no coordinates
- `--format jsonl`: one page record per line (for streaming/batch)

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

## Rules

1. Never fabricate OCR text. Only use the `text` field from results. If confidence < 0.5, state it.
2. Cite coordinates when relevant. Box coordinates are in `pageSpace`.
3. Use `--schema-version 1` for reproducible output. Do not parse help text.
4. Prefer `detect` first on large images, then `recognize --region` on areas of interest.
5. Check exit codes before parsing stdout. Non-zero exit means stdout may be empty; read stderr.
