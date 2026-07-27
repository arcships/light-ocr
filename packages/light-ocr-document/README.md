# @arcships/light-ocr-document

Compatibility entry for the PDF and multi-page API now built into
`@arcships/light-ocr`.

```bash
npm install @arcships/light-ocr
light-ocr-document report.pdf --pages 1-10 --format jsonl
light-ocr-document scan-1.png scan-2.jpg --format text
```

New applications should import from `@arcships/light-ocr`. This package keeps
the former import and `light-ocr-document` command working, but has no PDF
renderer dependency or installer of its own. The renderer is already included
in the platform package selected by npm, with no postinstall or runtime
download.

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
