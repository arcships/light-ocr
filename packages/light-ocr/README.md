# @arcships/light-ocr

The stable PP-OCRv6 Small entry for local image OCR on Node.js 22 and 24.

```bash
npm install @arcships/light-ocr
light-ocr image.png --format text
light-ocr doctor --json
```

The package exact-pins one model-free runtime, the Small model, and the native
component for the current platform. It has no install script and its complete
release closure is tested with npm offline and scripts disabled.

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

PDF and multi-page processing are intentionally separate:

```bash
npm install @arcships/light-ocr-document@next
light-ocr-document report.pdf --format jsonl
```

The Document package is preview software and runs its pinned PDF renderer's
prebuild installer. Keeping that dependency explicit prevents PDF tooling from
changing the stable image package's installation contract.
