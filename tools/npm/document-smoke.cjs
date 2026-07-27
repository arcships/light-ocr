'use strict';

const assert = require('node:assert/strict');
const path = require('node:path');
const { createRequire } = require('node:module');

const consumerRequire = createRequire(path.join(process.cwd(), 'package.json'));
const {
  createDocumentEngine,
  hasPdfSupport,
} = consumerRequire('@arcships/light-ocr');
const { createTextPdf } = require('./pdf-fixture.cjs');

async function main() {
  assert.equal(hasPdfSupport(), true, 'bundled PDFium runtime is unavailable');
  const engine = await createDocumentEngine({
    engineOptions: { execution: { provider: 'cpu' } },
  });
  try {
    const pages = [];
    for await (const page of engine.recognizePdf(createTextPdf(), { dpi: 150 })) {
      pages.push(page);
    }
    assert.equal(pages.length, 1);
    assert.equal(pages[0].source.kind, 'pdf');
    assert.match(
      pages[0].lines.map((line) => line.text).join(' '),
      /HELLO\s*123/i,
      'real PDF render did not survive OCR',
    );
    console.log(JSON.stringify({
      ok: true,
      package: '@arcships/light-ocr',
      pages: pages.length,
      text: pages[0].lines.map((line) => line.text).join(' '),
    }));
  } finally {
    await engine.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
