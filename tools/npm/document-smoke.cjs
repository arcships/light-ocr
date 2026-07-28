'use strict';

const assert = require('node:assert/strict');
const path = require('node:path');
const { createRequire } = require('node:module');

const consumerRequire = createRequire(path.join(process.cwd(), 'package.json'));
const {
  createDocumentEngine,
  hasPdfSupport,
} = consumerRequire('@arcships/light-ocr');
const {
  createNonEmbeddedCjkPdf,
  createTextPdf,
} = require('./pdf-fixture.cjs');

function nativePackageName() {
  const key = `${process.platform}-${process.arch}`;
  return {
    'darwin-arm64': '@arcships/light-ocr-darwin-arm64',
    'darwin-x64': '@arcships/light-ocr-darwin-x64',
    'linux-arm64': '@arcships/light-ocr-linux-arm64-gnu',
    'linux-x64': '@arcships/light-ocr-linux-x64-gnu',
    'win32-arm64': '@arcships/light-ocr-win32-arm64',
    'win32-x64': '@arcships/light-ocr-win32-x64',
  }[key];
}

async function main() {
  assert.equal(hasPdfSupport(), true, 'bundled PDFium runtime is unavailable');
  const nativePackage = nativePackageName();
  assert.ok(nativePackage, `unsupported smoke platform: ${process.platform}-${process.arch}`);
  const pdfium = consumerRequire(`${nativePackage}/pdfium`);
  const cjkPdf = createNonEmbeddedCjkPdf();
  const document = await pdfium.loadDocument(cjkPdf);
  const renderedPage = await document.getPage(0);
  try {
    const textObject = await renderedPage.getObject(0);
    assert.equal(textObject.isEmbedded, false);
    assert.equal(
      textObject.fontFamily,
      'Noto Sans CJK SC',
      'PDFium did not use the bundled fallback font',
    );
    const png = await renderedPage.render({ scale: 2 });
    assert.ok(png.length > 4_000, 'non-embedded CJK rendered as an empty page');
  } finally {
    await renderedPage.close();
    await document.destroy();
  }

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
    const cjkPages = [];
    for await (const page of engine.recognizePdf(cjkPdf, { dpi: 150 })) {
      cjkPages.push(page);
    }
    assert.equal(cjkPages.length, 1);
    assert.match(
      cjkPages[0].lines.map((line) => line.text).join(''),
      /中文测试/,
      'non-embedded CJK fallback render did not survive OCR',
    );
    console.log(JSON.stringify({
      ok: true,
      package: '@arcships/light-ocr',
      pages: pages.length,
      text: pages[0].lines.map((line) => line.text).join(' '),
      cjkText: cjkPages[0].lines.map((line) => line.text).join(' '),
    }));
  } finally {
    await engine.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
