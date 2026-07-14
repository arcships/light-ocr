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

function adapterError(code, message, detail, cause) {
  const error = new Error(message, cause === undefined ? undefined : { cause });
  error.name = 'OcrError';
  error.code = code;
  if (detail) error.detail = detail;
  return error;
}

function platformPackage() {
  const key = `${process.platform}-${process.arch}`;
  const packages = {
    'darwin-arm64': '@arcships/light-ocr-darwin-arm64',
    'darwin-x64': '@arcships/light-ocr-darwin-x64',
    'win32-x64': '@arcships/light-ocr-win32-x64',
  };
  if (key === 'linux-x64') {
    const report = process.report?.getReport?.();
    if (report?.header?.glibcVersionRuntime) {
      return '@arcships/light-ocr-linux-x64-gnu';
    }
    throw adapterError(
      'unsupported_platform',
      'light-ocr currently supports Linux x64 with glibc only',
      `${process.platform}-${process.arch}`,
    );
  }
  const packageName = packages[key];
  if (!packageName) {
    throw adapterError(
      'unsupported_platform',
      `light-ocr does not support ${key}`,
      key,
    );
  }
  return packageName;
}

function loadNative() {
  const candidates = candidatePaths();
  const binary = candidates.find((candidate) => fs.existsSync(candidate));
  if (binary) return require(binary);

  const packageName = platformPackage();
  try {
    return require(packageName);
  } catch (cause) {
    throw adapterError(
      'package_load_failed',
      `Unable to load ${packageName}`,
      'Reinstall @arcships/light-ocr without --omit=optional and verify that the ' +
        'current platform is supported.',
      cause,
    );
  }
}

module.exports = { loadNative };
