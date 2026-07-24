'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const path = require('node:path');

// Try to use workspace dependencies, fallback to local paths
let facade;
let runtime;
try {
  facade = require('../src/index.cjs');
  runtime = require('@arcships/light-ocr-runtime');
} catch {
  // Fallback to local runtime
  facade = require('../src/index.cjs');
  runtime = require(path.join(__dirname, '..', '..', 'runtime', 'src', 'index.cjs'));
}

const packageMetadata = require('../package.json');

test('small facade reuses the runtime API and error identity', () => {
  assert.strictEqual(facade.OcrError, runtime.OcrError);
});

test('small facade exposes PDF and document capabilities', () => {
  assert.equal(typeof facade.hasPdfSupport, 'function');
  assert.equal(typeof facade.recognizeDocument, 'function');
});

test('small facade rejects an unknown built-in model before native loading', async () => {
  await assert.rejects(
    facade.createEngine({ model: 'ppocrv6-medium' }),
    (error) => error instanceof runtime.OcrError
      && error.code === 'invalid_argument'
      && /ppocrv6-small/.test(error.message),
  );
});

test('small facade keeps model and bundlePath mutually exclusive', async () => {
  await assert.rejects(
    facade.createEngine({ model: 'ppocrv6-small', bundlePath: '/tmp/model' }),
    (error) => error instanceof runtime.OcrError
      && error.code === 'invalid_argument'
      && /cannot be used together/.test(error.message),
  );
});

test('all three tiers expose one API and one CLI contract', () => {
  const facades = [
    facade,
    require('../../light-ocr-tiny/src/index.cjs'),
    require('../../light-ocr-medium/src/index.cjs'),
  ];
  const clis = [
    require('../src/cli.cjs'),
    require('../../light-ocr-tiny/src/cli.cjs'),
    require('../../light-ocr-medium/src/cli.cjs'),
  ];
  for (const candidate of facades) {
    // All facades should have the base API
    assert.equal(typeof candidate.OcrError, 'function');
    assert.equal(typeof candidate.createEngine, 'function');
    assert.equal(typeof candidate.modelProfile, 'object');
    assert.strictEqual(candidate.OcrError, runtime.OcrError);
  }
  const cliKeys = Object.keys(clis[0]).sort();
  for (const candidate of clis.slice(1)) {
    assert.deepEqual(Object.keys(candidate).sort(), cliKeys);
    assert.strictEqual(candidate.OCR_ERROR_EXIT, clis[0].OCR_ERROR_EXIT);
  }
});
