'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');

const { createEngine, OcrError } = require('../src/index.cjs');

test('runtime requires an explicit model bundle', async () => {
  await assert.rejects(
    createEngine(),
    (error) => error instanceof OcrError
      && error.code === 'invalid_argument'
      && /bundlePath/.test(error.message),
  );
});

test('runtime rejects facade model selection', async () => {
  await assert.rejects(
    createEngine({ model: 'ppocrv6-small' }),
    (error) => error instanceof OcrError
      && error.code === 'invalid_argument'
      && /model-free/.test(error.message),
  );
});
