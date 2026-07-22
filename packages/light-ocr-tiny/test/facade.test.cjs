'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');

const facade = require('../src/index.cjs');
const runtime = require('@arcships/light-ocr-runtime');

test('tiny facade preserves the shared API and discloses its limits', () => {
  assert.strictEqual(facade.OcrError, runtime.OcrError);
  assert.deepEqual(facade.modelProfile.excludedLanguages, ['ja']);
  assert.equal(facade.modelProfile.languages, 49);
  assert.equal(facade.modelProfile.maturity, 'preview');
});

test('tiny facade rejects another built-in model before native loading', async () => {
  await assert.rejects(
    facade.createEngine({ model: 'ppocrv6-small' }),
    (error) => error instanceof runtime.OcrError
      && error.code === 'invalid_argument'
      && /ppocrv6-tiny/.test(error.message),
  );
});
