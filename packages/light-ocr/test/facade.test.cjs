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
