'use strict';

// Tests for the JS EXIF orientation parser (exif.cjs).
// These test the parser logic without needing the native C++ build.
// The C++ exif.cpp mirrors this logic; parity is verified by adapter tests
// that decode real JPEGs with EXIF orientation.

const assert = require('node:assert/strict');
const test = require('node:test');

const { parseExifOrientation, applyOrientation } = require('../js/exif.cjs');

test('parseExifOrientation: non-JPEG returns 1', () => {
  assert.equal(parseExifOrientation(Buffer.from([0x89, 0x50, 0x4e, 0x47])), 1); // PNG
  assert.equal(parseExifOrientation(Buffer.alloc(0)), 1);
  assert.equal(parseExifOrientation(null), 1);
});

test('parseExifOrientation: JPEG without EXIF returns 1', () => {
  // Minimal JPEG: SOI + EOI
  const jpeg = Buffer.from([0xff, 0xd8, 0xff, 0xd9]);
  assert.equal(parseExifOrientation(jpeg), 1);
});

test('parseExifOrientation: parses orientation 6 from real EXIF segment', () => {
  // Build a minimal JPEG with an APP1 EXIF segment containing orientation=6.
  const jpeg = makeExifJpeg(6);
  assert.equal(parseExifOrientation(jpeg), 6);
});

test('parseExifOrientation: parses all orientations 1-8', () => {
  for (let i = 1; i <= 8; i++) {
    const jpeg = makeExifJpeg(i);
    assert.equal(parseExifOrientation(jpeg), i, `orientation ${i}`);
  }
});

test('applyOrientation: orientation 1 is a no-op', () => {
  const pixels = { data: new Uint8Array([1,2,3, 4,5,6]), width: 2, height: 1 };
  const out = applyOrientation(pixels, 1);
  assert.equal(out, pixels);
});

test('applyOrientation: orientation 3 (180) reverses pixel order', () => {
  // 2x1 image: [A B] -> 180 -> [B A]
  const pixels = { data: new Uint8Array([1,2,3, 4,5,6]), width: 2, height: 1 };
  const out = applyOrientation(pixels, 3);
  assert.equal(out.width, 2);
  assert.equal(out.height, 1);
  // first pixel of output should be original last pixel
  assert.equal(out.data[0], 4);
  assert.equal(out.data[1], 5);
  assert.equal(out.data[2], 6);
  assert.equal(out.data[3], 1);
});

test('applyOrientation: orientation 6 (90 CW) swaps width/height', () => {
  // 2x1 image becomes 1x2
  const pixels = { data: new Uint8Array([1,2,3, 4,5,6]), width: 2, height: 1 };
  const out = applyOrientation(pixels, 6);
  assert.equal(out.width, 1);
  assert.equal(out.height, 2);
  assert.equal(out.data.length, 6);
});

test('applyOrientation: orientation 8 (90 CCW) swaps width/height', () => {
  const pixels = { data: new Uint8Array([1,2,3, 4,5,6]), width: 2, height: 1 };
  const out = applyOrientation(pixels, 8);
  assert.equal(out.width, 1);
  assert.equal(out.height, 2);
  assert.equal(out.data.length, 6);
});

test('applyOrientation: round-trip orientation 6 then 8 restores original', () => {
  const original = { data: new Uint8Array([1,2,3, 4,5,6, 7,8,9, 10,11,12]), width: 2, height: 2 };
  const rotated = applyOrientation(original, 6);
  const restored = applyOrientation(rotated, 8);
  assert.equal(restored.width, 2);
  assert.equal(restored.height, 2);
  assert.deepEqual(Array.from(restored.data), Array.from(original.data));
});

// Helper: build a minimal JPEG buffer with an EXIF APP1 segment
// containing the given orientation value (little-endian TIFF).
function makeExifJpeg(orientation) {
  // JPEG: SOI + APP1(EXIF) + EOI
  // APP1 structure: FFE1 + length(2) + "Exif\0\0" + TIFF header + IFD0
  const exifHeader = Buffer.from('Exif\0\0', 'ascii');
  // TIFF header: II (little-endian) + 42 + offset to IFD0 (8)
  const tiffHeader = Buffer.alloc(8);
  tiffHeader.write('II', 0, 'ascii');
  tiffHeader.writeUInt16LE(0x002a, 2);
  tiffHeader.writeUInt32LE(8, 4); // IFD0 offset from TIFF start
  // IFD0: 1 entry, 12 bytes each + 4 bytes next-IFD-offset
  const ifdCount = Buffer.alloc(2);
  ifdCount.writeUInt16LE(1, 0);
  // IFD entry: tag(2) + type(2) + count(4) + value(4)
  const entry = Buffer.alloc(12);
  entry.writeUInt16LE(0x0112, 0); // Orientation tag
  entry.writeUInt16LE(3, 2);      // SHORT type
  entry.writeUInt32LE(1, 4);      // count=1
  entry.writeUInt16LE(orientation, 8);
  const nextIfd = Buffer.alloc(4); // 0 = no next IFD

  const app1Data = Buffer.concat([exifHeader, tiffHeader, ifdCount, entry, nextIfd]);
  const app1Length = app1Data.length + 2; // +2 for the length field itself
  const app1Header = Buffer.alloc(4);
  app1Header[0] = 0xff;
  app1Header[1] = 0xe1;
  app1Header.writeUInt16BE(app1Length, 2);

  const soi = Buffer.from([0xff, 0xd8]);
  const eoi = Buffer.from([0xff, 0xd9]);
  return Buffer.concat([soi, app1Header, app1Data, eoi]);
}
