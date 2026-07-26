'use strict';

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

// Skip tests if pdfium-native is not available
let pdfiumAvailable = false;
try {
  require('pdfium-native');
  pdfiumAvailable = true;
} catch {}

describe('light-ocr-document', () => {
  it('should export expected functions', () => {
    const doc = require('../src/index.cjs');
    assert.equal(typeof doc.createDocumentEngine, 'function');
    assert.equal(typeof doc.getVersion, 'function');
    assert.equal(typeof doc.hasPdfSupport, 'function');
    assert.equal(typeof doc.OcrError, 'function');
  });

  it('should return version string', () => {
    const { getVersion } = require('../src/index.cjs');
    const version = getVersion();
    assert.equal(typeof version, 'string');
    assert.match(version, /^\d+\.\d+\.\d+$/);
  });

  it('should report PDF support status', () => {
    const { hasPdfSupport } = require('../src/index.cjs');
    const supported = hasPdfSupport();
    assert.equal(typeof supported, 'boolean');
    // Note: We can't assert the exact value since it depends on pdfium-native installation
  });

  it('should throw OcrError with correct properties', () => {
    const { OcrError } = require('../src/index.cjs');
    const err = new OcrError('test_code', 'test message', 'test detail');
    assert.equal(err.name, 'OcrError');
    assert.equal(err.code, 'test_code');
    assert.equal(err.message, 'test message');
    assert.equal(err.detail, 'test detail');
    assert.ok(err instanceof Error);
  });
});

// Integration tests (require actual engine and PDF)
if (pdfiumAvailable) {
  describe('DocumentEngine integration', () => {
    // These tests would require actual PDF files and OCR engine
    // For now, just verify the engine can be created
    it('should be able to create engine with options', async () => {
      // This test would need actual dependencies
      // Skipping for now
    });
  });
}
