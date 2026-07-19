'use strict';

const fs = require('node:fs');
const path = require('node:path');

const { loadNative } = require('./load-native.cjs');

const DEFAULT_MODEL = 'ppocrv6-small';
const MODEL_PACKAGE = '@arcships/light-ocr-model-ppocrv6-small';
const CPU_BUNDLE_ID = 'ppocrv6-small-onnx-20260714.2';
const APPLE_BUNDLE_ID = 'ppocrv6-small-apple-20260715.1';
const WEBGPU_BUNDLE_ID = 'ppocrv6-small-webgpu-20260719.1';
const NATIVE_BUNDLE_ID = 'ppocrv6-small-native-20260719.1';

class OcrError extends Error {
  constructor(code, message, detail) {
    super(message);
    this.name = 'OcrError';
    this.code = code;
    if (detail !== undefined && detail !== '') this.detail = detail;
  }
}

function normalizeNativeError(error) {
  if (error && error.name === 'OcrError') {
    Object.setPrototypeOf(error, OcrError.prototype);
  }
  return error;
}

function deepFreeze(value) {
  if (!value || typeof value !== 'object' || Object.isFrozen(value)) return value;
  for (const child of Object.values(value)) deepFreeze(child);
  return Object.freeze(value);
}

function validateSignal(signal) {
  if (
    signal === null ||
    typeof signal !== 'object' ||
    typeof signal.aborted !== 'boolean' ||
    typeof signal.addEventListener !== 'function' ||
    typeof signal.removeEventListener !== 'function'
  ) {
    throw new OcrError('invalid_argument', 'signal must be an AbortSignal');
  }
}

function abortReason(signal) {
  return signal.reason === undefined
    ? new DOMException('The operation was aborted', 'AbortError')
    : signal.reason;
}

function resolveBuiltInBundle(model, requireApple) {
  if (model !== DEFAULT_MODEL) {
    throw new OcrError(
      'invalid_argument',
      `model must be ${JSON.stringify(DEFAULT_MODEL)}`,
    );
  }
  let manifestPath;
  try {
    manifestPath = require.resolve(`${MODEL_PACKAGE}/bundle/manifest.json`);
  } catch (cause) {
    throw new OcrError(
      'package_load_failed',
      `Unable to locate the built-in ${DEFAULT_MODEL} model`,
      `Reinstall ${MODEL_PACKAGE}; ${cause instanceof Error ? cause.message : String(cause)}`,
    );
  }
  let manifest;
  try {
    manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
  } catch (cause) {
    throw new OcrError(
      'package_load_failed',
      'Unable to read the built-in model manifest',
      cause instanceof Error ? cause.message : String(cause),
    );
  }
  const compatibleBundleIds = requireApple
    ? [APPLE_BUNDLE_ID, NATIVE_BUNDLE_ID]
    : [CPU_BUNDLE_ID, APPLE_BUNDLE_ID, WEBGPU_BUNDLE_ID, NATIVE_BUNDLE_ID];
  if (!compatibleBundleIds.includes(manifest.bundleId)) {
    throw new OcrError(
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
    throw new OcrError('invalid_argument', 'createEngine options must be an object');
  }
  const hasModel = Object.prototype.hasOwnProperty.call(options, 'model');
  const hasBundlePath = Object.prototype.hasOwnProperty.call(options, 'bundlePath');
  if (hasModel && hasBundlePath) {
    throw new OcrError(
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

class OcrEngineImpl {
  #native;
  #closePromise;

  constructor(nativeEngine) {
    this.#native = nativeEngine;
    this.info = deepFreeze(nativeEngine.info);
    Object.defineProperty(this, 'info', { writable: false, configurable: false });
  }

  recognize(image, options = {}) {
    return this.#recognize('recognize', image, options);
  }

  recognizeEncoded(data, options = {}) {
    return this.#recognize('recognizeEncoded', data, options);
  }

  #recognize(nativeMethod, image, options) {
    let signal;
    let nativeOptions;
    try {
      if (options === null || typeof options !== 'object' || Array.isArray(options)) {
        throw new OcrError('invalid_argument', 'recognize options must be an object');
      }
      signal = options.signal;
      if (signal !== undefined) {
        validateSignal(signal);
        if (signal.aborted) return Promise.reject(abortReason(signal));
      }
      nativeOptions = { ...options };
      delete nativeOptions.signal;
    } catch (error) {
      return Promise.reject(normalizeNativeError(error));
    }

    let operation;
    try {
      operation = this.#native[nativeMethod](image, nativeOptions);
    } catch (error) {
      return Promise.reject(normalizeNativeError(error));
    }

    return new Promise((resolve, reject) => {
      let settled = false;
      const cleanup = () => {
        if (signal !== undefined) signal.removeEventListener('abort', onAbort);
      };
      const settle = (callback, value) => {
        if (settled) return;
        settled = true;
        cleanup();
        callback(value);
      };
      const onAbort = () => {
        if (settled) return;
        try {
          this.#native.cancel(operation.requestId);
        } catch {
          // Public cancellation still wins. Native teardown owns any remaining work.
        }
        settle(reject, abortReason(signal));
      };

      operation.promise.then(
        (value) => settle(resolve, value),
        (error) => settle(reject, normalizeNativeError(error)),
      );

      if (signal !== undefined) {
        signal.addEventListener('abort', onAbort, { once: true });
        if (signal.aborted) onAbort();
      }
    });
  }

  close() {
    if (this.#closePromise === undefined) {
      try {
        this.#closePromise = Promise.resolve(this.#native.close()).catch((error) => {
          throw normalizeNativeError(error);
        });
      } catch (error) {
        this.#closePromise = Promise.reject(normalizeNativeError(error));
      }
    }
    return this.#closePromise;
  }
}

let nativeRuntime;

async function createEngine(options) {
  try {
    const resolvedOptions = resolveCreateOptions(options);
    if (!nativeRuntime) nativeRuntime = loadNative();
    const nativeEngine = await nativeRuntime.binding.createEngine(
      resolvedOptions,
      nativeRuntime.runtimePolicy,
    );
    return new OcrEngineImpl(nativeEngine);
  } catch (error) {
    throw normalizeNativeError(error);
  }
}

module.exports = { createEngine, OcrError };
