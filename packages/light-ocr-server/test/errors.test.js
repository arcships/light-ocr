'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const multer = require('multer');

const { statusForOcrError, errorHandler } = require('../src/errors');

function responseRecorder() {
  return {
    statusCode: undefined,
    body: undefined,
    status(code) {
      this.statusCode = code;
      return this;
    },
    json(payload) {
      this.body = payload;
    },
  };
}

test('statusForOcrError maps stable engine error codes', () => {
  assert.equal(statusForOcrError('queue_full'), 429);
  assert.equal(statusForOcrError('resource_limit_exceeded'), 413);
  assert.equal(statusForOcrError('invalid_argument'), 400);
  assert.equal(statusForOcrError('invalid_image'), 422);
  assert.equal(statusForOcrError('unsupported_pixel_format'), 400);
  assert.equal(statusForOcrError('environment_closing'), 503);
  assert.equal(statusForOcrError('something_new'), 500);
});

test('errorHandler preserves an OcrError code and message', () => {
  const error = Object.assign(new Error('boom'), {
    name: 'OcrError',
    code: 'invalid_argument',
  });
  const response = responseRecorder();
  errorHandler(error, {}, response, () => {});
  assert.equal(response.statusCode, 400);
  assert.deepEqual(response.body, { error: 'invalid_argument', message: 'boom' });
});

test('errorHandler maps Multer file-size failures to 413', () => {
  const response = responseRecorder();
  errorHandler(new multer.MulterError('LIMIT_FILE_SIZE'), {}, response, () => {});
  assert.equal(response.statusCode, 413);
  assert.deepEqual(response.body, { error: 'file_too_large' });
});

test('errorHandler maps other Multer failures to 400', () => {
  const response = responseRecorder();
  errorHandler(
    new multer.MulterError('LIMIT_UNEXPECTED_FILE', 'image'),
    {},
    response,
    () => {},
  );
  assert.equal(response.statusCode, 400);
  assert.equal(response.body.error, 'invalid_upload');
});

test('errorHandler hides unknown internal errors', () => {
  const response = responseRecorder();
  errorHandler(new Error('secret detail'), {}, response, () => {});
  assert.equal(response.statusCode, 500);
  assert.deepEqual(response.body, { error: 'internal_error' });
});
