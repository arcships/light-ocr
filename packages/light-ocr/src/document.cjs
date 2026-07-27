'use strict';

const fs = require('node:fs/promises');
const path = require('node:path');

const { OcrError } = require('@arcships/light-ocr-runtime');

const DEFAULTS = Object.freeze({
  dpi: 150,
  maxPages: 100,
  maxPagePixels: 4096 * 4096,
  maxTotalPixels: 100 * 1024 * 1024,
  maxFileBytes: 100 * 1024 * 1024,
});

let pdfium;
let pdfiumLoaded = false;
let defaultCreateEngine;

function platformPdfiumPackage() {
  const packages = {
    'darwin-arm64': '@arcships/light-ocr-darwin-arm64',
    'darwin-x64': '@arcships/light-ocr-darwin-x64',
    'linux-arm64': '@arcships/light-ocr-linux-arm64-gnu',
    'linux-x64': '@arcships/light-ocr-linux-x64-gnu',
    'win32-arm64': '@arcships/light-ocr-win32-arm64',
    'win32-x64': '@arcships/light-ocr-win32-x64',
  };
  return packages[`${process.platform}-${process.arch}`];
}

function loadPdfium() {
  if (!pdfiumLoaded) {
    pdfiumLoaded = true;
    try {
      const developmentModule = process.env.LIGHT_OCR_PDFIUM_MODULE;
      if (developmentModule) {
        pdfium = require(path.resolve(developmentModule));
      } else {
        const packageName = platformPdfiumPackage();
        if (!packageName) {
          throw new Error(`unsupported platform ${process.platform}-${process.arch}`);
        }
        pdfium = require(`${packageName}/pdfium`);
      }
    } catch {
      // Workspace tests may use the upstream module directly. Published packages
      // do not depend on it: production PDFium lives in the platform npm package.
      try {
        pdfium = require('pdfium-native');
      } catch {
        pdfium = undefined;
      }
    }
  }
  return pdfium;
}

function hasPdfSupport() {
  return loadPdfium() !== undefined;
}

function getVersion() {
  return require('../package.json').version;
}

function invalidArgument(message) {
  return new OcrError('invalid_argument', message);
}

function positiveInteger(value, name, fallback, maximum = Number.MAX_SAFE_INTEGER) {
  if (value === undefined) return fallback;
  if (!Number.isSafeInteger(value) || value < 1 || value > maximum) {
    throw invalidArgument(`${name} must be an integer between 1 and ${maximum}`);
  }
  return value;
}

function normalizeOptions(options) {
  if (options === undefined) options = {};
  if (options === null || typeof options !== 'object' || Array.isArray(options)) {
    throw invalidArgument('document options must be an object');
  }
  const normalized = {
    ...options,
    dpi: positiveInteger(options.dpi, 'dpi', DEFAULTS.dpi, 600),
    maxPages: positiveInteger(options.maxPages, 'maxPages', DEFAULTS.maxPages, 10000),
    maxPagePixels: positiveInteger(
      options.maxPagePixels,
      'maxPagePixels',
      DEFAULTS.maxPagePixels,
    ),
    maxTotalPixels: positiveInteger(
      options.maxTotalPixels,
      'maxTotalPixels',
      DEFAULTS.maxTotalPixels,
    ),
    maxFileBytes: positiveInteger(
      options.maxFileBytes,
      'maxFileBytes',
      DEFAULTS.maxFileBytes,
    ),
  };
  if (normalized.dpi < 36) {
    throw invalidArgument('dpi must be an integer between 36 and 600');
  }
  if (options.pageRange !== undefined) {
    const range = options.pageRange;
    if (
      range === null
      || typeof range !== 'object'
      || Array.isArray(range)
      || !Number.isSafeInteger(range.start)
      || !Number.isSafeInteger(range.end)
      || range.start < 1
      || range.end < range.start
    ) {
      throw invalidArgument('pageRange must contain 1-based integers with start <= end');
    }
  }
  return normalized;
}

function throwIfAborted(signal) {
  if (signal?.aborted) {
    throw signal.reason === undefined
      ? new DOMException('The operation was aborted', 'AbortError')
      : signal.reason;
  }
}

function isBytes(value) {
  return value instanceof Uint8Array;
}

function isPdf(value) {
  return value?.length >= 4
    && value[0] === 0x25
    && value[1] === 0x50
    && value[2] === 0x44
    && value[3] === 0x46;
}

function mediaType(value) {
  if (
    value?.length >= 4
    && value[0] === 0x89
    && value[1] === 0x50
    && value[2] === 0x4e
    && value[3] === 0x47
  ) {
    return 'image/png';
  }
  if (value?.length >= 3 && value[0] === 0xff && value[1] === 0xd8 && value[2] === 0xff) {
    return 'image/jpeg';
  }
  return 'application/octet-stream';
}

async function readInput(source, maxFileBytes) {
  if (typeof source === 'string') {
    const stats = await fs.stat(source);
    if (stats.size > maxFileBytes) {
      throw new OcrError(
        'resource_limit_exceeded',
        `File size ${stats.size} exceeds maxFileBytes ${maxFileBytes}`,
      );
    }
    return fs.readFile(source);
  }
  if (!isBytes(source)) {
    throw invalidArgument('document inputs must be file paths or Uint8Array values');
  }
  if (source.byteLength > maxFileBytes) {
    throw new OcrError(
      'resource_limit_exceeded',
      `Input size ${source.byteLength} exceeds maxFileBytes ${maxFileBytes}`,
    );
  }
  return source;
}

function linesFrom(result) {
  return result.lines.map((line, index) => ({
    id: `L${index}`,
    text: line.text,
    confidence: line.confidence,
    box: line.box,
  }));
}

function pageRect(page, box) {
  if (
    box
    && [box.left, box.bottom, box.right, box.top].every(Number.isFinite)
  ) {
    return {
      x: box.left,
      y: box.bottom,
      width: box.right - box.left,
      height: box.top - box.bottom,
    };
  }
  return { x: 0, y: 0, width: page.width, height: page.height };
}

async function* processPdf(engine, input, options) {
  const renderer = loadPdfium();
  if (!renderer) {
    throw new OcrError(
      'unsupported_capability',
      'PDF support is unavailable; reinstall @arcships/light-ocr without --omit=optional',
    );
  }
  const pdf = await readInput(input, options.maxFileBytes);
  const document = await renderer.loadDocument(
    Buffer.from(pdf.buffer, pdf.byteOffset, pdf.byteLength),
  );
  let totalPixels = 0;
  try {
    const start = options.pageRange?.start ?? 1;
    const end = Math.min(options.pageRange?.end ?? document.pageCount, document.pageCount);
    const requestedPages = Math.max(0, end - start + 1);
    if (start > document.pageCount) {
      throw invalidArgument(`pageRange starts after the document's ${document.pageCount} pages`);
    }
    if (requestedPages > options.maxPages) {
      throw new OcrError(
        'resource_limit_exceeded',
        `Page count ${requestedPages} exceeds maxPages ${options.maxPages}`,
      );
    }

    for (let pageNumber = start; pageNumber <= end; pageNumber++) {
      throwIfAborted(options.signal);
      const page = await document.getPage(pageNumber - 1);
      let result;
      try {
        const scale = options.dpi / 72;
        const renderedWidth = Math.ceil(page.width * scale);
        const renderedHeight = Math.ceil(page.height * scale);
        const renderedPixels = renderedWidth * renderedHeight;
        if (
          !Number.isSafeInteger(renderedPixels)
          || renderedPixels > options.maxPagePixels
        ) {
          throw new OcrError(
            'resource_limit_exceeded',
            `Page ${pageNumber} rendered pixels ${renderedPixels} `
              + `exceeds maxPagePixels ${options.maxPagePixels}`,
          );
        }
        totalPixels += renderedPixels;
        if (!Number.isSafeInteger(totalPixels) || totalPixels > options.maxTotalPixels) {
          throw new OcrError(
            'resource_limit_exceeded',
            `Total rendered pixels ${totalPixels} exceeds maxTotalPixels `
              + `${options.maxTotalPixels}`,
          );
        }

        const renderStart = performance.now();
        const png = await page.render({ scale });
        const renderUs = Math.round((performance.now() - renderStart) * 1000);
        throwIfAborted(options.signal);
        const ocrStart = performance.now();
        const ocr = await engine.recognizeEncoded(png, {
          ...options.ocrOptions,
          signal: options.signal,
        });
        const ocrUs = Math.round((performance.now() - ocrStart) * 1000);
        result = {
          index: pageNumber - 1,
          width: ocr.imageWidth,
          height: ocr.imageHeight,
          coordinateSpace: 'pageSpace',
          structure: 'ocr-order',
          lines: linesFrom(ocr),
          source: {
            kind: 'pdf',
            mediaType: 'application/pdf',
            identity: { pageIndex: pageNumber - 1 },
            appliedTransforms: {
              pdf: {
                rotation: Number(page.rotation ?? 0) * 90,
                mediaBox: { x: 0, y: 0, width: page.width, height: page.height },
                cropBox: pageRect(page, page.cropBox),
                dpi: options.dpi,
                scale,
              },
            },
          },
          timingUs: { total: renderUs + ocrUs, decode: renderUs, ocr: ocrUs },
          modelBundleId: ocr.modelBundleId,
        };
      } finally {
        await page.close();
      }
      yield result;
    }
  } finally {
    await document.destroy();
  }
}

async function* processImages(engine, inputs, options) {
  if (inputs.length > options.maxPages) {
    throw new OcrError(
      'resource_limit_exceeded',
      `Page count ${inputs.length} exceeds maxPages ${options.maxPages}`,
    );
  }
  let totalPixels = 0;
  for (let index = 0; index < inputs.length; index++) {
    throwIfAborted(options.signal);
    const image = await readInput(inputs[index], options.maxFileBytes);
    const started = performance.now();
    const ocr = await engine.recognizeEncoded(image, {
      ...options.ocrOptions,
      applyExif: true,
      signal: options.signal,
    });
    const ocrUs = Math.round((performance.now() - started) * 1000);
    const pixels = ocr.imageWidth * ocr.imageHeight;
    if (!Number.isSafeInteger(pixels) || pixels > options.maxPagePixels) {
      throw new OcrError(
        'resource_limit_exceeded',
        `Image ${index + 1} pixels ${pixels} exceeds maxPagePixels ${options.maxPagePixels}`,
      );
    }
    totalPixels += pixels;
    if (!Number.isSafeInteger(totalPixels) || totalPixels > options.maxTotalPixels) {
      throw new OcrError(
        'resource_limit_exceeded',
        `Total image pixels ${totalPixels} exceeds maxTotalPixels ${options.maxTotalPixels}`,
      );
    }
    yield {
      index,
      width: ocr.imageWidth,
      height: ocr.imageHeight,
      coordinateSpace: 'pageSpace',
      structure: 'ocr-order',
      lines: linesFrom(ocr),
      source: {
        kind: 'image',
        mediaType: mediaType(image),
        identity: { index },
        appliedTransforms: {},
      },
      timingUs: { total: ocrUs, decode: 0, ocr: ocrUs },
      modelBundleId: ocr.modelBundleId,
    };
  }
}

class DocumentEngine {
  #engine;
  #ownsEngine;
  #closed = false;

  constructor(engine, ownsEngine) {
    this.#engine = engine;
    this.#ownsEngine = ownsEngine;
  }

  async *recognizeDocument(source, options) {
    if (this.#closed) throw new OcrError('invalid_engine', 'Document engine is closed');
    const normalized = normalizeOptions(options);
    if (Array.isArray(source)) {
      if (source.length === 0) throw invalidArgument('document source array must not be empty');
      yield* processImages(this.#engine, source, normalized);
      return;
    }
    if (typeof source !== 'string' && !isBytes(source)) {
      throw invalidArgument(
        'document source must be a file path, Uint8Array, or a non-empty array of them',
      );
    }
    if (
      (typeof source === 'string' && path.extname(source).toLowerCase() === '.pdf')
      || (isBytes(source) && isPdf(source))
    ) {
      yield* processPdf(this.#engine, source, normalized);
      return;
    }
    yield* processImages(this.#engine, [source], normalized);
  }

  recognizePdf(source, options) {
    if (this.#closed) throw new OcrError('invalid_engine', 'Document engine is closed');
    return processPdf(this.#engine, source, normalizeOptions(options));
  }

  recognizeImages(sources, options) {
    if (this.#closed) throw new OcrError('invalid_engine', 'Document engine is closed');
    if (!Array.isArray(sources) || sources.length === 0) {
      throw invalidArgument('image sources must be a non-empty array');
    }
    return processImages(this.#engine, sources, normalizeOptions(options));
  }

  async close() {
    if (this.#closed) return;
    this.#closed = true;
    if (this.#ownsEngine) await this.#engine.close();
  }
}

async function createDocumentEngine(options = {}) {
  if (options === null || typeof options !== 'object' || Array.isArray(options)) {
    throw invalidArgument('createDocumentEngine options must be an object');
  }
  const ownsEngine = options.engine === undefined;
  const engine = options.engine ?? await defaultCreateEngine(options.engineOptions);
  return new DocumentEngine(engine, ownsEngine);
}

async function* recognizeDocument(source, options = {}) {
  if (options === null || typeof options !== 'object' || Array.isArray(options)) {
    throw invalidArgument('recognizeDocument options must be an object');
  }
  const documentEngine = await createDocumentEngine({
    engine: options.engine,
    engineOptions: options.engineOptions,
  });
  try {
    yield* documentEngine.recognizeDocument(source, options);
  } finally {
    await documentEngine.close();
  }
}

function createDocumentApi(createEngine) {
  if (typeof createEngine !== 'function') {
    throw new TypeError('createDocumentApi requires createEngine');
  }
  defaultCreateEngine = createEngine;
  return Object.freeze({
    createDocumentEngine,
    getVersion,
    hasPdfSupport,
    recognizeDocument,
  });
}

module.exports = { createDocumentApi };
