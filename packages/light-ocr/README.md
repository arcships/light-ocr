# @arcships/light-ocr

The stable PP-OCRv6 Small entry for local image and PDF OCR on Node.js 22 and 24.

```bash
npm install @arcships/light-ocr
light-ocr image.png --format text
light-ocr report.pdf --pages 1-10 --format jsonl
light-ocr doctor --json
```

The package exact-pins one model-free runtime, the Small model, and the native
component, PDFium renderer, and Noto Sans CJK SC fallback font for the current
platform. It has no install script and its complete release closure is tested
with npm offline and scripts disabled.

```js
const { createEngine } = require('@arcships/light-ocr');

const engine = await createEngine();
try {
  const result = await engine.recognizeEncoded(imageBytes);
  console.log(result.lines);
} finally {
  await engine.close();
}
```

The main package exports `recognizeDocument()` and `createDocumentEngine()`.
PDFium's native files and the checksum-pinned fallback font are inside the
platform npm package, so neither install nor runtime performs a secondary
download or requires a system Chinese font.
