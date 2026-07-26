'use strict';

// Integration tests for document/OCR functionality.
// These tests require the native runtime and model bundle.
// Separated from cli.test.cjs because they are slower (~1.7s per test).

const assert = require('node:assert/strict');
const test = require('node:test');
const path = require('node:path');
const fs = require('node:fs');
const os = require('node:os');

const FIXTURE = path.resolve(__dirname, '../../../packages/light-ocr-server/test/fixtures/hello-123.png');

// Test the recognizeDocument API directly (no stdout capture issues)
test('recognizeDocument: processes single image buffer', async () => {
  const facade = require('../../../packages/light-ocr/src/index.cjs');
  const imageBuffer = fs.readFileSync(FIXTURE);
  const pages = [];
  for await (const page of facade.recognizeDocument(imageBuffer, { engine: undefined })) {
    pages.push(page);
  }
  assert.equal(pages.length, 1);
  assert.equal(pages[0].index, 0);
  assert.equal(pages[0].coordinateSpace, 'pageSpace');
  assert.equal(pages[0].structure, 'ocr-order');
  assert.ok(pages[0].lines.length > 0);
  assert.equal(pages[0].lines[0].text, 'HELLO 123');
  assert.ok(pages[0].lines[0].confidence > 0.5);
  assert.equal(pages[0].lines[0].box.length, 4);
  assert.equal(pages[0].source.kind, 'image');
  assert.equal(typeof pages[0].timingUs.total, 'number');
  assert.equal(typeof pages[0].modelBundleId, 'string');
});

test('recognizeDocument: processes file path', async () => {
  const facade = require('../../../packages/light-ocr/src/index.cjs');
  const pages = [];
  for await (const page of facade.recognizeDocument(FIXTURE)) {
    pages.push(page);
  }
  assert.equal(pages.length, 1);
  assert.equal(pages[0].lines[0].text, 'HELLO 123');
});

test('recognizeDocument: processes multiple images', async () => {
  const facade = require('../../../packages/light-ocr/src/index.cjs');
  const pages = [];
  for await (const page of facade.recognizeDocument([FIXTURE, FIXTURE])) {
    pages.push(page);
  }
  assert.equal(pages.length, 2);
  assert.equal(pages[0].index, 0);
  assert.equal(pages[1].index, 1);
  assert.equal(pages[0].lines[0].text, 'HELLO 123');
  assert.equal(pages[1].lines[0].text, 'HELLO 123');
});

test('recognizeDocument: line IDs are stable', async () => {
  const facade = require('../../../packages/light-ocr/src/index.cjs');
  const pages = [];
  for await (const page of facade.recognizeDocument(FIXTURE)) {
    pages.push(page);
  }
  const line = pages[0].lines[0];
  assert.match(line.id, /^L\d+$/);
});

test('recognizeDocument: hasPdfSupport returns boolean', () => {
  const facade = require('../../../packages/light-ocr/src/index.cjs');
  assert.equal(typeof facade.hasPdfSupport, 'function');
  const supported = facade.hasPdfSupport();
  assert.equal(typeof supported, 'boolean');
});

test('recognizeDocument: rejects nonexistent file', async () => {
  const facade = require('../../../packages/light-ocr/src/index.cjs');
  await assert.rejects(
    async () => {
      for await (const _ of facade.recognizeDocument('nonexistent.png')) { /* drain */ }
    },
    (err) => err.code === 'ENOENT',
  );
});
