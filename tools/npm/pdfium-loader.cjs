'use strict';

const path = require('node:path');

const addonDirectory = __dirname;
if (process.platform === 'win32') {
  process.env.PATH = `${addonDirectory};${process.env.PATH ?? ''}`;
}

const addon = require(path.join(addonDirectory, 'pdfium.node'));

module.exports = Object.freeze({
  loadDocument(input, password) {
    return addon.loadDocument(input, password);
  },
});
