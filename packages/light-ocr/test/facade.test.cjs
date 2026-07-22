'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');

const facade = require('../src/index.cjs');
const runtime = require('@arcships/light-ocr-runtime');
const packageMetadata = require('../package.json');

test('small facade reuses the runtime API and error identity', () => {
  assert.strictEqual(facade.OcrError, runtime.OcrError);
  assert.equal(
    packageMetadata.dependencies['@arcships/light-ocr-runtime'],
    require('../../runtime/package.json').version,
  );
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
    assert.deepEqual(Object.keys(candidate).sort(), [
      'OcrError',
      'createEngine',
      'modelProfile',
    ]);
    assert.strictEqual(candidate.OcrError, runtime.OcrError);
  }
  const cliKeys = Object.keys(clis[0]).sort();
  for (const candidate of clis.slice(1)) {
    assert.deepEqual(Object.keys(candidate).sort(), cliKeys);
    assert.strictEqual(candidate.OCR_ERROR_EXIT, clis[0].OCR_ERROR_EXIT);
  }
});
