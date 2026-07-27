'use strict';

const lightOcr = require('@arcships/light-ocr');

module.exports = Object.freeze({
  createDocumentEngine: lightOcr.createDocumentEngine,
  getVersion: lightOcr.getVersion,
  hasPdfSupport: lightOcr.hasPdfSupport,
  recognizeDocument: lightOcr.recognizeDocument,
  OcrError: lightOcr.OcrError,
});
