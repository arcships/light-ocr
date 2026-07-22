'use strict';

const express = require('express');
const multer = require('multer');

const MAX_FILE_BYTES = 20 * 1024 * 1024;
const DEFAULT_MAX_CONCURRENT_UPLOADS = 4;

const upload = multer({
  storage: multer.memoryStorage(),
  limits: { fileSize: MAX_FILE_BYTES },
});

function ocrRouter(engine, options = {}) {
  const maxConcurrentUploads =
    options.maxConcurrentUploads ?? DEFAULT_MAX_CONCURRENT_UPLOADS;
  let activeUploads = 0;

  const router = express.Router();
  router.post(
    '/ocr',
    (request, response, next) => {
      if (activeUploads >= maxConcurrentUploads) {
        response.status(429).json({ error: 'too_many_uploads' });
        return;
      }
      activeUploads += 1;
      let released = false;
      const release = () => {
        if (released) return;
        released = true;
        activeUploads -= 1;
      };
      response.on('finish', release);
      response.on('close', release);
      next();
    },
    upload.single('image'),
    async (request, response, next) => {
      if (!request.file || request.file.buffer.length === 0) {
        response.status(400).json({ error: 'missing_image' });
        return;
      }
      try {
        const result = await engine.recognizeEncoded(request.file.buffer);
        response.status(200).json({ lines: result.lines });
      } catch (error) {
        next(error);
      }
    },
  );
  return router;
}

module.exports = ocrRouter;
