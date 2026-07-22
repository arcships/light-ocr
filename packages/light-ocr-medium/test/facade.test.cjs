'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');

const facade = require('../src/index.cjs');
const runtime = require('@arcships/light-ocr-runtime');

test('medium facade preserves the shared API and reports preview maturity', () => {
  assert.strictEqual(facade.OcrError, runtime.OcrError);
  assert.equal(facade.modelProfile.languages, 50);
  assert.equal(facade.modelProfile.maturity, 'preview');
});

test('medium facade rejects another built-in model before native loading', async () => {
  await assert.rejects(
    facade.createEngine({ model: 'ppocrv6-small' }),
    (error) => error instanceof runtime.OcrError
      && error.code === 'invalid_argument'
      && /ppocrv6-medium/.test(error.message),
  );
});
