'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');

const { statusForOcrError, errorHandler } = require('../src/errors');

test('statusForOcrError maps known OcrError codes', () => {
  assert.equal(statusForOcrError('queue_full'), 429);
  assert.equal(statusForOcrError('resource_limit_exceeded'), 413);
  assert.equal(statusForOcrError('invalid_argument'), 400);
  assert.equal(statusForOcrError('invalid_image'), 422);
  assert.equal(statusForOcrError('unsupported_pixel_format'), 400);
});

test('statusForOcrError defaults unknown codes to 500', () => {
  assert.equal(statusForOcrError('internal_error'), 500);
  assert.equal(statusForOcrError('something_new'), 500);
});

test('errorHandler responds with mapped status and code for an OcrError', () => {
  const err = Object.assign(new Error('boom'), { name: 'OcrError', code: 'invalid_argument' });
  let statusCode;
  let body;
  const res = {
    status(code) {
      statusCode = code;
      return this;
    },
    json(payload) {
      body = payload;
    },
  };
  errorHandler(err, {}, res, () => {});
  assert.equal(statusCode, 400);
  assert.deepEqual(body, { error: 'invalid_argument', message: 'boom' });
});

test('errorHandler responds 413 for multer file-size errors', () => {
  const multer = require('multer');
  const err = new multer.MulterError('LIMIT_FILE_SIZE');
  let statusCode;
  let body;
  const res = {
    status(code) {
      statusCode = code;
      return this;
    },
    json(payload) {
      body = payload;
    },
  };
  errorHandler(err, {}, res, () => {});
  assert.equal(statusCode, 413);
  assert.deepEqual(body, { error: 'file_too_large' });
});

test('errorHandler responds 500 for unknown errors', () => {
  const err = new Error('unexpected');
  let statusCode;
  let body;
  const res = {
    status(code) {
      statusCode = code;
      return this;
    },
    json(payload) {
      body = payload;
    },
  };
  errorHandler(err, {}, res, () => {});
  assert.equal(statusCode, 500);
  assert.deepEqual(body, { error: 'internal_error' });
});
