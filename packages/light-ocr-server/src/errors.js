'use strict';

const multer = require('multer');

const STATUS_BY_CODE = {
  queue_full: 429,
  resource_limit_exceeded: 413,
  invalid_argument: 400,
  invalid_image: 422,
  unsupported_pixel_format: 400,
  environment_closing: 503,
};

function statusForOcrError(code) {
  return STATUS_BY_CODE[code] ?? 500;
}

function errorHandler(error, request, response, next) {
  if (error instanceof multer.MulterError) {
    if (error.code === 'LIMIT_FILE_SIZE') {
      response.status(413).json({ error: 'file_too_large' });
      return;
    }
    response.status(400).json({ error: 'invalid_upload', message: error.message });
    return;
  }
  if (error && error.name === 'OcrError') {
    response.status(statusForOcrError(error.code)).json({
      error: error.code,
      message: error.message,
    });
    return;
  }
  response.status(500).json({ error: 'internal_error' });
}

module.exports = { statusForOcrError, errorHandler };
