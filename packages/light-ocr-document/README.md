# @arcships/light-ocr-document

Preview PDF and multi-page image OCR for Node.js 22 and 24.

```bash
npm install @arcships/light-ocr-document@next
light-ocr-document report.pdf --pages 1-10 --format jsonl
light-ocr-document scan-1.png scan-2.jpg --format text
```

This package is intentionally separate from stable `@arcships/light-ocr`.
Installation runs the pinned `pdfium-native` installer, which downloads a
checksum-verified prebuilt binary for supported platforms and may require a
compiler if no prebuild is available. Processing does not upload documents or
use a network service.

On Windows, install from PowerShell or Command Prompt. The pinned renderer's
installer invokes `tar` with native Windows paths; Git Bash may resolve its own
GNU tar first and misinterpret the drive-letter colon.

```js
const { recognizeDocument } = require('@arcships/light-ocr-document');

for await (const page of recognizeDocument('report.pdf', {
  dpi: 150,
  pageRange: { start: 1, end: 10 },
  maxPages: 100,
})) {
  console.log(page.index, page.lines);
}
```

For repeated work, create one engine and close it explicitly:

```js
const { createDocumentEngine } = require('@arcships/light-ocr-document');

const engine = await createDocumentEngine();
try {
  for await (const page of engine.recognizeDocument(['one.png', 'two.jpg'])) {
    console.log(page);
  }
} finally {
  await engine.close();
}
```

Default limits are 100 pages, 100 MiB per input, 4096 × 4096 rendered pixels
per page, 100 Mi rendered pixels per request, and 150 DPI. Limits can only be
raised explicitly. The CLI returns stable non-zero exit codes for invalid
arguments, unsupported capability, resource limits, package loading, inference,
and internal failures.
