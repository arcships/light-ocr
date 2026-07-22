'use strict';

// Minimal JPEG EXIF orientation parser (D106, D-N1-5).
// Reads the orientation tag from the JPEG APP1 segment and applies the
// corresponding pixel transform. Zero-dependency, stb-style.
//
// Only orientation (tag 0x0112) is read. Other EXIF fields are skipped.
// PNG eXIf is not handled here; PNG has no EXIF orientation in v1.
//
// References:
// - JEITA CP-3451C (Exif 2.3) section 4.6.4 (APP1 structure)
// - TIFF tag 0x0112 Orientation

// Parse the EXIF orientation value from a JPEG buffer.
// Returns 1..8 if found, or 1 (normal) if not present / not a JPEG.
function parseExifOrientation(buffer) {
  if (!buffer || buffer.length < 4) return 1;
  // JPEG must start with SOI marker 0xFFD8
  if (buffer[0] !== 0xff || buffer[1] !== 0xd8) return 1;

  let offset = 2;
  while (offset + 1 < buffer.length) {
    // Each marker: 0xFF then marker code
    if (buffer[offset] !== 0xff) return 1;
    const marker = buffer[offset + 1];
    offset += 2;

    // SOI (D8), EOI (D9), RSTn (D0-D7), TEM (01): no payload
    if (marker === 0xd8 || marker === 0xd9) return 1;
    if (marker >= 0xd0 && marker <= 0xd7) continue;
    if (marker === 0x01) continue;

    // SOS (DA): start of scan — EXIF would be before this
    if (marker === 0xda) return 1;

    // All other markers have a 2-byte length (including the length bytes)
    if (offset + 1 >= buffer.length) return 1;
    const length = (buffer[offset] << 8) | buffer[offset + 1];
    if (length < 2 || offset + length > buffer.length) return 1;

    // APP1 marker is 0xFFE1
    if (marker === 0xe1) {
      const orientation = tryParseApp1(buffer, offset, length);
      if (orientation) return orientation;
    }

    offset += length;
  }
  return 1;
}

// Try to parse APP1 as EXIF. Returns orientation 1..8 or 0 if not EXIF.
function tryParseApp1(buffer, dataOffset, segmentLength) {
  // APP1 data starts after the 2-byte length field
  // EXIF header: "Exif\0\0" (6 bytes)
  const exifHeader = dataOffset + 2;
  if (exifHeader + 6 > dataOffset + segmentLength) return 0;
  if (buffer[exifHeader] !== 0x45 || buffer[exifHeader + 1] !== 0x78 ||
      buffer[exifHeader + 2] !== 0x69 || buffer[exifHeader + 3] !== 0x66 ||
      buffer[exifHeader + 4] !== 0x00 || buffer[exifHeader + 5] !== 0x00) {
    return 0; // not EXIF (could be XMP)
  }

  // TIFF header starts here
  const tiffStart = exifHeader + 6;
  if (tiffStart + 8 > dataOffset + segmentLength) return 0;

  // Byte order: II (little-endian) or MM (big-endian)
  const littleEndian = buffer[tiffStart] === 0x49 && buffer[tiffStart + 1] === 0x49;
  const bigEndian = buffer[tiffStart] === 0x4d && buffer[tiffStart + 1] === 0x4d;
  if (!littleEndian && !bigEndian) return 0;
  const le = littleEndian;

  // Magic number 42 (0x002A)
  const magic = readU16(buffer, tiffStart + 2, le);
  if (magic !== 0x002a) return 0;

  // Offset to IFD0 from TIFF start
  const ifdOffset = tiffStart + readU32(buffer, tiffStart + 4, le);
  if (ifdOffset + 2 > dataOffset + segmentLength) return 0;

  const entryCount = readU16(buffer, ifdOffset, le);
  for (let i = 0; i < entryCount; i++) {
    const entryOffset = ifdOffset + 2 + i * 12;
    if (entryOffset + 12 > dataOffset + segmentLength) break;
    const tag = readU16(buffer, entryOffset, le);
    if (tag === 0x0112) { // Orientation
      const type = readU16(buffer, entryOffset + 2, le);
      const count = readU32(buffer, entryOffset + 4, le);
      if (type === 3 && count === 1) { // SHORT
        const value = readU16(buffer, entryOffset + 8, le);
        if (value >= 1 && value <= 8) return value;
      }
      return 0;
    }
  }
  return 0;
}

function readU16(buffer, offset, littleEndian) {
  if (littleEndian) return buffer[offset] | (buffer[offset + 1] << 8);
  return (buffer[offset] << 8) | buffer[offset + 1];
}

function readU32(buffer, offset, littleEndian) {
  if (littleEndian) {
    return (buffer[offset]) |
           (buffer[offset + 1] << 8) |
           (buffer[offset + 2] << 16) |
           (buffer[offset + 3] << 24);
  }
  return (buffer[offset] << 24) |
         (buffer[offset + 1] << 16) |
         (buffer[offset + 2] << 8) |
         (buffer[offset + 3]);
}

// Apply EXIF orientation to RGB pixel data.
// input: { data: Uint8Array (RGB), width, height }
// orientation: 1..8
// Returns { data, width, height } in pageSpace (orientation-corrected).
function applyOrientation(pixels, orientation) {
  if (orientation === 1) return pixels; // normal, no transform

  const { data, width: w, height: h } = pixels;
  const channels = 3;

  switch (orientation) {
    case 2: // flip horizontal
      return flipHorizontal(data, w, h, channels);
    case 3: // rotate 180
      return rotate180(data, w, h, channels);
    case 4: // flip vertical
      return flipVertical(data, w, h, channels);
    case 5: // transpose (flip horizontal + rotate 270 CW)
      return transpose(data, w, h, channels);
    case 6: // rotate 90 CW
      return rotate90CW(data, w, h, channels);
    case 7: // transverse (flip horizontal + rotate 90 CW)
      return transverse(data, w, h, channels);
    case 8: // rotate 90 CCW (= 270 CW)
      return rotate90CCW(data, w, h, channels);
    default:
      return pixels;
  }
}

function alloc(w, h, channels) {
  return { data: new Uint8Array(w * h * channels), width: w, height: h };
}

function flipHorizontal(data, w, h, c) {
  const out = alloc(w, h, c);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const src = (y * w + x) * c;
      const dst = (y * w + (w - 1 - x)) * c;
      for (let i = 0; i < c; i++) out.data[dst + i] = data[src + i];
    }
  }
  return out;
}

function flipVertical(data, w, h, c) {
  const out = alloc(w, h, c);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const src = (y * w + x) * c;
      const dst = ((h - 1 - y) * w + x) * c;
      for (let i = 0; i < c; i++) out.data[dst + i] = data[src + i];
    }
  }
  return out;
}

function rotate180(data, w, h, c) {
  const out = alloc(w, h, c);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const src = (y * w + x) * c;
      const dst = ((h - 1 - y) * w + (w - 1 - x)) * c;
      for (let i = 0; i < c; i++) out.data[dst + i] = data[src + i];
    }
  }
  return out;
}

function rotate90CW(data, w, h, c) {
  // new dimensions: h x w
  const out = alloc(h, w, c);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const src = (y * w + x) * c;
      // (x, y) -> (h-1-y, x) in the new w'=h, h'=w grid
      const dst = (x * h + (h - 1 - y)) * c;
      for (let i = 0; i < c; i++) out.data[dst + i] = data[src + i];
    }
  }
  return out;
}

function rotate90CCW(data, w, h, c) {
  const out = alloc(h, w, c);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const src = (y * w + x) * c;
      // (x, y) -> (y, w-1-x)
      const dst = ((w - 1 - x) * h + y) * c;
      for (let i = 0; i < c; i++) out.data[dst + i] = data[src + i];
    }
  }
  return out;
}

function transpose(data, w, h, c) {
  // transpose: (x,y) -> (y,x)
  const out = alloc(h, w, c);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const src = (y * w + x) * c;
      const dst = (x * h + y) * c;
      for (let i = 0; i < c; i++) out.data[dst + i] = data[src + i];
    }
  }
  return out;
}

function transverse(data, w, h, c) {
  // transverse: (x,y) -> (h-1-y, w-1-x)
  const out = alloc(h, w, c);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const src = (y * w + x) * c;
      const dst = ((h - 1 - y) * h + (w - 1 - x)) * c;
      for (let i = 0; i < c; i++) out.data[dst + i] = data[src + i];
    }
  }
  return out;
}

module.exports = { parseExifOrientation, applyOrientation };
