'use strict';

const fs = require('node:fs');
const path = require('node:path');

const addonDirectory = __dirname;
const fontDirectory = path.join(addonDirectory, 'fonts');
const fallbackFont = path.join(fontDirectory, 'NotoSansCJKsc-Regular.otf');
if (!fs.existsSync(fallbackFont)) {
  throw new Error(`Bundled PDF fallback font is missing: ${fallbackFont}`);
}

if (process.platform === 'win32') {
  process.env.PATH = `${addonDirectory};${process.env.PATH ?? ''}`;
}

const previousFontDirectory = process.env.LIGHT_OCR_PDFIUM_FONT_DIR;
process.env.LIGHT_OCR_PDFIUM_FONT_DIR = fontDirectory;
let addon;
try {
  addon = require(path.join(addonDirectory, 'pdfium.node'));
} finally {
  if (previousFontDirectory === undefined) {
    delete process.env.LIGHT_OCR_PDFIUM_FONT_DIR;
  } else {
    process.env.LIGHT_OCR_PDFIUM_FONT_DIR = previousFontDirectory;
  }
}

module.exports = Object.freeze({
  loadDocument(input, password) {
    return addon.loadDocument(input, password);
  },
});
