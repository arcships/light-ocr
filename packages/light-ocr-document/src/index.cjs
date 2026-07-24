'use strict';

// Lazy load dependencies
let createEngine = null;
let pdfium = null;
let pdfiumLoaded = false;
let fs = null;
let path = null;

function loadDependencies() {
  if (!createEngine) {
    try {
      ({ createEngine } = require('@arcships/light-ocr-runtime'));
    } catch {
      // Runtime not available - will be provided via engine option
    }
  }
  
  if (!pdfiumLoaded) {
    pdfiumLoaded = true;
    try {
      pdfium = require('pdfium-native');
    } catch {
      // pdfium-native not available
      pdfium = null;
    }
  }
  
  if (!fs) {
    fs = require('node:fs/promises');
  }
  
  if (!path) {
    path = require('node:path');
  }
}

function getVersion() {
  try {
    const pkg = require('../package.json');
    return pkg.version;
  } catch {
    return '0.0.0';
  }
}

function hasPdfSupport() {
  loadDependencies();
  return pdfium !== null;
}

async function* processPdf(engine, pdfBuffer, options = {}) {
  loadDependencies();
  if (!pdfium) {
    throw new OcrError('unsupported_capability', 'PDF support not available. Install pdfium-native.');
  }

  const {
    format = 'json',
    pageRange,
    dpi = 150,
    maxPages = 100,
    maxPagePixels = 4096 * 4096,
    maxTotalPixels = 100 * 1024 * 1024,
    signal,
    ocrOptions = {}
  } = options;

  let totalPixels = 0;
  let processedPages = 0;

  // Open PDF document
  const doc = await pdfium.loadDocument(pdfBuffer);
  
  try {
    const pageCount = doc.pageCount;
    
    // Apply page range
    const start = pageRange?.start ? Math.max(1, pageRange.start) : 1;
    const end = pageRange?.end ? Math.min(pageCount, pageRange.end) : pageCount;
    
    // Check page limits
    if (end - start + 1 > maxPages) {
      throw new OcrError('resource_limit_exceeded', `Page count ${end - start + 1} exceeds maxPages ${maxPages}`);
    }

    for (let i = start; i <= end; i++) {
      // Check abort signal
      if (signal?.aborted) {
        throw new OcrError('internal_error', 'Operation aborted');
      }

      const page = await doc.getPage(i - 1); // 0-indexed
      
      // Get page dimensions
      const { width, height } = page;
      const pagePixels = width * height;
      
      // Check pixel limits
      if (pagePixels > maxPagePixels) {
        throw new OcrError('resource_limit_exceeded', `Page ${i} pixels ${pagePixels} exceeds maxPagePixels ${maxPagePixels}`);
      }
      
      totalPixels += pagePixels;
      if (totalPixels > maxTotalPixels) {
        throw new OcrError('resource_limit_exceeded', `Total pixels ${totalPixels} exceeds maxTotalPixels ${maxTotalPixels}`);
      }

      // Render page to PNG
      const renderStart = Date.now();
      const scale = dpi / 72; // PDF default is 72 DPI
      const pngBuffer = await page.render({ scale });
      const renderTime = (Date.now() - renderStart) * 1000;

      // OCR the rendered image
      const ocrStart = Date.now();
      const ocrResult = await engine.recognizeEncoded(pngBuffer, ocrOptions);
      const ocrTime = (Date.now() - ocrStart) * 1000;

      // Close page to free native memory
      await page.close();

      processedPages++;

      // Build page result
      const pageResult = {
        index: i - 1, // 0-indexed
        width: ocrResult.imageWidth,
        height: ocrResult.imageHeight,
        coordinateSpace: 'pageSpace',
        structure: 'ocr-order',
        lines: ocrResult.lines.map((line, idx) => ({
          id: `L${idx}`,
          text: line.text,
          confidence: line.confidence,
          box: line.box
        })),
        source: {
          kind: 'pdf',
          mediaType: 'application/pdf',
          identity: { pageIndex: i - 1 },
          appliedTransforms: {
            pdf: {
              rotation: 0, // TODO: Get from PDF metadata
              mediaBox: { x: 0, y: 0, width, height },
              cropBox: { x: 0, y: 0, width, height },
              dpi,
              scale: dpi / 72 // PDF default is 72 DPI
            }
          }
        },
        timingUs: {
          total: renderTime + ocrTime,
          decode: renderTime,
          ocr: ocrTime
        }
      };

      yield pageResult;
    }
  } finally {
    doc.destroy();
  }
}

async function* processImages(engine, imageBuffers, options = {}) {
  const {
    format = 'json',
    maxPagePixels = 4096 * 4096,
    signal,
    ocrOptions = {}
  } = options;

  for (let i = 0; i < imageBuffers.length; i++) {
    // Check abort signal
    if (signal?.aborted) {
      throw new OcrError('internal_error', 'Operation aborted');
    }

    const buffer = imageBuffers[i];
    
    // OCR the image
    const ocrStart = Date.now();
    const ocrResult = await engine.recognizeEncoded(buffer, {
      ...ocrOptions,
      applyExif: true
    });
    const ocrTime = (Date.now() - ocrStart) * 1000; // Convert to microseconds

    // Build page result
    const pageResult = {
      index: i,
      width: ocrResult.imageWidth,
      height: ocrResult.imageHeight,
      coordinateSpace: 'pageSpace',
      structure: 'ocr-order',
      lines: ocrResult.lines.map((line, idx) => ({
        id: `L${idx}`,
        text: line.text,
        confidence: line.confidence,
        box: line.box
      })),
      source: {
        kind: 'image',
        mediaType: 'image/png', // TODO: Detect actual media type
        identity: { index: i },
        appliedTransforms: {
          exif: {
            orientation: 1, // TODO: Get from EXIF
            applied: false
          }
        }
      },
      timingUs: {
        total: ocrTime,
        decode: 0,
        ocr: ocrTime
      }
    };

    yield pageResult;
  }
}

class OcrError extends Error {
  constructor(code, message, detail) {
    super(message);
    this.name = 'OcrError';
    this.code = code;
    this.detail = detail;
  }
}

class DocumentEngineImpl {
  constructor(engine, pdfiumOptions = {}) {
    this._engine = engine;
    this._pdfiumOptions = pdfiumOptions;
    this._closed = false;
  }

  async *_recognizePdf(source, options = {}) {
    if (this._closed) {
      throw new OcrError('invalid_engine', 'Engine is closed');
    }

    let pdfBuffer;
    if (typeof source === 'string') {
      pdfBuffer = await fs.readFile(source);
    } else {
      pdfBuffer = source;
    }

    // Check file size
    const maxFileBytes = options.maxFileBytes || 100 * 1024 * 1024; // 100MB
    if (pdfBuffer.byteLength > maxFileBytes) {
      throw new OcrError('resource_limit_exceeded', `File size ${pdfBuffer.byteLength} exceeds maxFileBytes ${maxFileBytes}`);
    }

    yield* processPdf(this._engine, pdfBuffer, options);
  }

  async *_recognizeImages(sources, options = {}) {
    if (this._closed) {
      throw new OcrError('invalid_engine', 'Engine is closed');
    }

    const buffers = [];
    for (const source of sources) {
      if (typeof source === 'string') {
        buffers.push(await fs.readFile(source));
      } else {
        buffers.push(source);
      }
    }

    yield* processImages(this._engine, buffers, options);
  }

  async *_recognizeDocument(source, options = {}) {
    if (this._closed) {
      throw new OcrError('invalid_engine', 'Engine is closed');
    }

    // Determine source type
    if (Array.isArray(source)) {
      yield* this._recognizeImages(source, options);
    } else if (typeof source === 'string') {
      // Detect file type by extension
      const ext = path.extname(source).toLowerCase();
      if (ext === '.pdf') {
        yield* this._recognizePdf(source, options);
      } else {
        // Treat as single image
        yield* this._recognizeImages([source], options);
      }
    } else {
      // Buffer - try to detect PDF magic bytes
      const isPdf = source[0] === 0x25 && source[1] === 0x50 && source[2] === 0x44 && source[3] === 0x46;
      if (isPdf) {
        yield* this._recognizePdf(source, options);
      } else {
        yield* this._recognizeImages([source], options);
      }
    }
  }

  recognizePdf(source, options) {
    return this._recognizePdf(source, options);
  }

  recognizeImages(sources, options) {
    return this._recognizeImages(sources, options);
  }

  recognizeDocument(source, options) {
    return this._recognizeDocument(source, options);
  }

  async close() {
    if (this._closed) return;
    this._closed = true;
    // Note: We don't close the engine here since it might be shared
    // The caller is responsible for closing the engine they provided
  }
}

async function createDocumentEngine(options = {}) {
  loadDependencies();
  
  let engine = options.engine;
  
  if (!engine) {
    if (!createEngine) {
      throw new OcrError('package_load_failed', 
        'Could not create OCR engine. Provide engine option or install @arcships/light-ocr-runtime.'
      );
    }
    
    // Try to load from peer dependency
    try {
      const path = require('node:path');
      const bundlePath = options.bundlePath || 
        require.resolve('@arcships/light-ocr-model-ppocrv6-small');
      engine = await createEngine({ bundlePath });
    } catch (err) {
      throw new OcrError('package_load_failed', 
        'Could not create OCR engine. Provide engine option or install @arcships/light-ocr.',
        err.message
      );
    }
  }

  return new DocumentEngineImpl(engine, options.pdfium || {});
}

module.exports = {
  createDocumentEngine,
  getVersion,
  hasPdfSupport,
  OcrError
};
