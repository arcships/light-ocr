'use strict';

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const {
  createDocumentEngine,
  getVersion,
  hasPdfSupport,
  recognizeDocument,
  OcrError,
} = require('../src/index.cjs');
const { createTextPdf } = require('../../../tools/npm/pdf-fixture.cjs');

const PNG = Uint8Array.from([0x89, 0x50, 0x4e, 0x47, 0x00]);

function fakeResult(width = 20, height = 10) {
  return {
    imageWidth: width,
    imageHeight: height,
    modelBundleId: 'test-bundle',
    lines: [{
      text: 'HELLO 123',
      confidence: 0.99,
      box: [
        { x: 0, y: 0 },
        { x: width, y: 0 },
        { x: width, y: height },
        { x: 0, y: height },
      ],
    }],
  };
}

function fakeEngine({ width = 20, height = 10 } = {}) {
  let closes = 0;
  return {
    get closes() {
      return closes;
    },
    async recognizeEncoded(_input, options) {
      if (options?.signal?.aborted) {
        throw new DOMException('aborted', 'AbortError');
      }
      return fakeResult(width, height);
    },
    async close() {
      closes++;
    },
  };
}

async function collect(generator) {
  const values = [];
  for await (const value of generator) values.push(value);
  return values;
}

describe('light-ocr-document surface', () => {
  it('exports the complete preview surface', () => {
    assert.equal(typeof createDocumentEngine, 'function');
    assert.equal(typeof recognizeDocument, 'function');
    assert.equal(typeof hasPdfSupport(), 'boolean');
    assert.match(getVersion(), /^\d+\.\d+\.\d+$/);
    assert.equal(typeof OcrError, 'function');
  });

  it('processes image pages without closing a borrowed OCR engine', async () => {
    const borrowed = fakeEngine();
    const engine = await createDocumentEngine({ engine: borrowed });
    const pages = await collect(engine.recognizeImages([PNG, PNG]));
    await engine.close();

    assert.equal(pages.length, 2);
    assert.equal(pages[0].source.kind, 'image');
    assert.equal(pages[0].source.mediaType, 'image/png');
    assert.deepEqual(pages[0].source.identity, { index: 0 });
    assert.equal(pages[0].lines[0].text, 'HELLO 123');
    assert.equal(borrowed.closes, 0);
  });

  it('enforces page, file, page-pixel, and total-pixel limits', async () => {
    const engine = await createDocumentEngine({ engine: fakeEngine({ width: 20, height: 10 }) });

    await assert.rejects(
      collect(engine.recognizeImages([PNG, PNG], { maxPages: 1 })),
      (error) => error.code === 'resource_limit_exceeded',
    );
    await assert.rejects(
      collect(engine.recognizeImages([PNG], { maxFileBytes: 4 })),
      (error) => error.code === 'resource_limit_exceeded',
    );
    await assert.rejects(
      collect(engine.recognizeImages([PNG], { maxPagePixels: 199 })),
      (error) => error.code === 'resource_limit_exceeded',
    );
    await assert.rejects(
      collect(engine.recognizeImages([PNG, PNG], { maxTotalPixels: 399 })),
      (error) => error.code === 'resource_limit_exceeded',
    );
  });

  it('validates options and propagates cancellation', async () => {
    const engine = await createDocumentEngine({ engine: fakeEngine() });
    await assert.rejects(
      collect(engine.recognizeDocument(PNG, { dpi: 35 })),
      (error) => error.code === 'invalid_argument',
    );
    await assert.rejects(
      collect(engine.recognizeDocument([])),
      (error) => error.code === 'invalid_argument',
    );
    const controller = new AbortController();
    controller.abort();
    await assert.rejects(
      collect(engine.recognizeDocument(PNG, { signal: controller.signal })),
      (error) => error.name === 'AbortError',
    );
  });

  it('renders a real in-memory PDF when pdfium is installed', {
    skip: !hasPdfSupport() && 'pdfium-native is not installed',
  }, async () => {
    const engine = await createDocumentEngine({ engine: fakeEngine() });
    const pages = await collect(engine.recognizePdf(createTextPdf(), { dpi: 72 }));
    await engine.close();

    assert.equal(pages.length, 1);
    assert.equal(pages[0].source.kind, 'pdf');
    assert.equal(pages[0].source.appliedTransforms.pdf.dpi, 72);
    assert.equal(pages[0].lines[0].text, 'HELLO 123');
  });

  it('checks PDF pixel limits after applying DPI scale', {
    skip: !hasPdfSupport() && 'pdfium-native is not installed',
  }, async () => {
    const engine = await createDocumentEngine({ engine: fakeEngine() });
    await assert.rejects(
      collect(engine.recognizePdf(createTextPdf(), {
        dpi: 600,
        maxPagePixels: 1_000_000,
      })),
      (error) => error.code === 'resource_limit_exceeded',
    );
  });
});
