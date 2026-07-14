'use strict';

const fs = require('node:fs');
const path = require('node:path');

function candidatePaths() {
  const candidates = [];
  if (process.env.LIGHT_OCR_NODE_BINARY) {
    candidates.push(path.resolve(process.env.LIGHT_OCR_NODE_BINARY));
  }
  candidates.push(path.join(__dirname, 'native', 'light_ocr_node.node'));
  candidates.push(
    path.join(
      __dirname,
      '..',
      'prebuilds',
      `${process.platform}-${process.arch}`,
      'light_ocr_node.node',
    ),
  );
  return candidates;
}

function loadNative() {
  const candidates = candidatePaths();
  const binary = candidates.find((candidate) => fs.existsSync(candidate));
  if (!binary) {
    const error = new Error(
      `No light-ocr native binary for ${process.platform}-${process.arch}. ` +
        'Set LIGHT_OCR_NODE_BINARY for a development build.',
    );
    error.code = 'LIGHT_OCR_NATIVE_NOT_FOUND';
    error.candidates = candidates;
    throw error;
  }
  return require(binary);
}

module.exports = { loadNative };
