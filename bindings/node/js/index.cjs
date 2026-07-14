'use strict';

const { loadNative } = require('./load-native.cjs');

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

class OcrEngineImpl {
  #native;
  #closePromise;

  constructor(nativeEngine) {
    this.#native = nativeEngine;
    this.info = deepFreeze(nativeEngine.info);
    Object.defineProperty(this, 'info', { writable: false, configurable: false });
  }

  recognize(image, options = {}) {
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
      operation = this.#native.recognize(image, nativeOptions);
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

let binding;

async function createEngine(options) {
  try {
    if (!binding) binding = loadNative();
    const nativeEngine = await binding.createEngine(options);
    return new OcrEngineImpl(nativeEngine);
  } catch (error) {
    throw normalizeNativeError(error);
  }
}

module.exports = { createEngine, OcrError };
