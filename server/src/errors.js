'use strict';

const multer = require('multer');

const STATUS_BY_CODE = {
  queue_full: 429,
  resource_limit_exceeded: 413,
  invalid_argument: 400,
  invalid_image: 422,
  unsupported_pixel_format: 400,
};

function statusForOcrError(code) {
  return STATUS_BY_CODE[code] ?? 500;
}

function errorHandler(err, req, res, next) {
  if (err instanceof multer.MulterError && err.code === 'LIMIT_FILE_SIZE') {
    res.status(413).json({ error: 'file_too_large' });
    return;
  }
  if (err && err.name === 'OcrError') {
    res.status(statusForOcrError(err.code)).json({ error: err.code, message: err.message });
    return;
  }
  res.status(500).json({ error: 'internal_error' });
}

module.exports = { statusForOcrError, errorHandler };
