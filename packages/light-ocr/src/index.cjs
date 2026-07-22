'use strict';

const fs = require('node:fs');
const path = require('node:path');

const runtime = require('@arcships/light-ocr-runtime');

const DEFAULT_MODEL = 'ppocrv6-small';
const MODEL_PACKAGE = '@arcships/light-ocr-model-ppocrv6-small';
const CPU_BUNDLE_ID = 'ppocrv6-small-onnx-20260714.2';
const APPLE_BUNDLE_ID = 'ppocrv6-small-apple-20260715.1';
const WEBGPU_BUNDLE_ID = 'ppocrv6-small-webgpu-20260719.1';
const NATIVE_BUNDLE_ID = 'ppocrv6-small-native-20260719.1';

function resolveBuiltInBundle(model, requireApple) {
  if (model !== DEFAULT_MODEL) {
    throw new runtime.OcrError(
      'invalid_argument',
      `model must be ${JSON.stringify(DEFAULT_MODEL)}`,
    );
  }
  let manifestPath;
  try {
    manifestPath = require.resolve(`${MODEL_PACKAGE}/bundle/manifest.json`);
  } catch (cause) {
    throw new runtime.OcrError(
      'package_load_failed',
      `Unable to locate the built-in ${DEFAULT_MODEL} model`,
      `Reinstall ${MODEL_PACKAGE}; ${cause instanceof Error ? cause.message : String(cause)}`,
    );
  }
  let manifest;
  try {
    manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
  } catch (cause) {
    throw new runtime.OcrError(
      'package_load_failed',
      'Unable to read the built-in model manifest',
      cause instanceof Error ? cause.message : String(cause),
    );
  }
  const compatibleBundleIds = requireApple
    ? [APPLE_BUNDLE_ID, NATIVE_BUNDLE_ID]
    : [CPU_BUNDLE_ID, APPLE_BUNDLE_ID, WEBGPU_BUNDLE_ID, NATIVE_BUNDLE_ID];
  if (!compatibleBundleIds.includes(manifest.bundleId)) {
    throw new runtime.OcrError(
      'package_load_failed',
      'The installed model package is incompatible with this light-ocr release',
      `expected ${compatibleBundleIds.join(' or ')}, received ${String(manifest.bundleId)}`,
    );
  }
  return path.dirname(manifestPath);
}

function resolveCreateOptions(options) {
  if (options === undefined) options = {};
  if (options === null || typeof options !== 'object' || Array.isArray(options)) {
    throw new runtime.OcrError('invalid_argument', 'createEngine options must be an object');
  }
  const hasModel = Object.prototype.hasOwnProperty.call(options, 'model');
  const hasBundlePath = Object.prototype.hasOwnProperty.call(options, 'bundlePath');
  if (hasModel && hasBundlePath) {
    throw new runtime.OcrError(
      'invalid_argument',
      'model and bundlePath cannot be used together',
    );
  }
  if (hasBundlePath) return options;
  const model = hasModel ? options.model : DEFAULT_MODEL;
  const requireApple = options.execution?.provider === 'apple';
  const resolved = { ...options, bundlePath: resolveBuiltInBundle(model, requireApple) };
  delete resolved.model;
  return resolved;
}

async function createEngine(options) {
  return runtime.createEngine(resolveCreateOptions(options));
}

module.exports = { createEngine, OcrError: runtime.OcrError };
