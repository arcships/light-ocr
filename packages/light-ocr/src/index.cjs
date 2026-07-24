'use strict';

const path = require('node:path');

// Try to use workspace dependencies, fallback to local paths
let createModelFacade;
try {
  ({ createModelFacade } = require('@arcships/light-ocr-runtime/facade'));
} catch {
  // Fallback to local runtime
  const facadePath = path.join(__dirname, '..', '..', 'runtime', 'src', 'facade.cjs');
  ({ createModelFacade } = require(facadePath));
}

const fs = require('node:fs');

const facade = createModelFacade({
  model: 'ppocrv6-small',
  modelPackage: '@arcships/light-ocr-model-ppocrv6-small',
  compatibleBundleIds: [
    'ppocrv6-small-onnx-20260714.2',
    'ppocrv6-small-apple-20260715.1',
    'ppocrv6-small-webgpu-20260719.1',
    'ppocrv6-small-native-20260719.1',
  ],
  appleBundleIds: [
    'ppocrv6-small-apple-20260715.1',
    'ppocrv6-small-native-20260719.1',
  ],
  profile: {
    tier: 'small',
    model: 'ppocrv6-small',
    bundleId: 'ppocrv6-small-native-20260719.1',
    languages: 50,
    excludedLanguages: [],
    dictionaryEntries: 18709,
    maturity: 'stable',
  },
});

// Lazy load pdfium-native
let pdfium = null;
let pdfiumLoaded = false;

function loadPdfium() {
  if (pdfiumLoaded) return pdfium;
  pdfiumLoaded = true;
  try {
    pdfium = require('pdfium-native');
  } catch {
    pdfium = null;
  }
  return pdfium;
}

function hasPdfSupport() {
  return loadPdfium() !== null;
}

async function* processPdf(engine, pdfBuffer, options = {}) {
  const pdfiumNative = loadPdfium();
  if (!pdfiumNative) {
    throw new facade.OcrError('unsupported_capability', 'PDF support not available. Install pdfium-native.');
  }

  const {
    pageRange,
    dpi = 150,
    maxPages = 100,
    maxPagePixels = 4096 * 4096,
    maxTotalPixels = 100 * 1024 * 1024,
    maxFileBytes = 100 * 1024 * 1024,
    signal,
    ocrOptions = {}
  } = options;

  // Check file size
  if (pdfBuffer.byteLength > maxFileBytes) {
    throw new facade.OcrError('resource_limit_exceeded', 
      `File size ${pdfBuffer.byteLength} exceeds maxFileBytes ${maxFileBytes}`);
  }

  let totalPixels = 0;

  // Open PDF document
  const doc = await pdfiumNative.open(pdfBuffer);
  
  try {
    const pageCount = doc.pageCount;
    
    // Apply page range
    const start = pageRange?.start ? Math.max(1, pageRange.start) : 1;
    const end = pageRange?.end ? Math.min(pageCount, pageRange.end) : pageCount;
    
    // Check page limits
    if (end - start + 1 > maxPages) {
      throw new facade.OcrError('resource_limit_exceeded', 
        `Page count ${end - start + 1} exceeds maxPages ${maxPages}`);
    }

    for (let i = start; i <= end; i++) {
      // Check abort signal
      if (signal?.aborted) {
        throw new facade.OcrError('internal_error', 'Operation aborted');
      }

      const page = doc.page(i);
      
      // Get page dimensions
      const { width, height } = page.size;
      const pagePixels = width * height;
      
      // Check pixel limits
      if (pagePixels > maxPagePixels) {
        throw new facade.OcrError('resource_limit_exceeded', 
          `Page ${i} pixels ${pagePixels} exceeds maxPagePixels ${maxPagePixels}`);
      }
      
      totalPixels += pagePixels;
      if (totalPixels > maxTotalPixels) {
        throw new facade.OcrError('resource_limit_exceeded', 
          `Total pixels ${totalPixels} exceeds maxTotalPixels ${maxTotalPixels}`);
      }

      // Render page to PNG
      const renderStart = Date.now();
      const pngBuffer = await page.render({ dpi });
      const renderTime = (Date.now() - renderStart) * 1000;

      // OCR the rendered image
      const ocrStart = Date.now();
      const ocrResult = await engine.recognizeEncoded(pngBuffer, ocrOptions);
      const ocrTime = (Date.now() - ocrStart) * 1000;

      // Build page result
      const pageResult = {
        index: i - 1,
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
              rotation: 0,
              mediaBox: { x: 0, y: 0, width, height },
              cropBox: { x: 0, y: 0, width, height },
              dpi,
              scale: dpi / 72
            }
          }
        },
        timingUs: {
          total: renderTime + ocrTime,
          decode: renderTime,
          ocr: ocrTime
        },
        modelBundleId: ocrResult.modelBundleId
      };

      yield pageResult;
    }
  } finally {
    doc.destroy();
  }
}

async function* processImages(engine, imageBuffers, options = {}) {
  const { signal, ocrOptions = {} } = options;

  for (let i = 0; i < imageBuffers.length; i++) {
    if (signal?.aborted) {
      throw new facade.OcrError('internal_error', 'Operation aborted');
    }

    const buffer = imageBuffers[i];
    
    const ocrStart = Date.now();
    const ocrResult = await engine.recognizeEncoded(buffer, {
      ...ocrOptions,
      applyExif: true
    });
    const ocrTime = (Date.now() - ocrStart) * 1000;

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
        mediaType: 'image/png',
        identity: { index: i },
        appliedTransforms: {
          exif: { orientation: 1, applied: false }
        }
      },
      timingUs: {
        total: ocrTime,
        decode: 0,
        ocr: ocrTime
      },
      modelBundleId: ocrResult.modelBundleId
    };

    yield pageResult;
  }
}

async function* recognizeDocument(source, options = {}) {
  // Create engine if not provided
  let engine = options.engine;
  let engineCreated = false;
  
  if (!engine) {
    engine = await facade.createEngine();
    engineCreated = true;
  }
  
  try {
    let buffers;
    let isPdf = false;
    
    if (Array.isArray(source)) {
      // Multiple images
      buffers = [];
      for (const s of source) {
        if (typeof s === 'string') {
          buffers.push(fs.readFileSync(s));
        } else {
          buffers.push(s);
        }
      }
    } else if (typeof source === 'string') {
      // File path
      const ext = path.extname(source).toLowerCase();
      if (ext === '.pdf') {
        isPdf = true;
        buffers = [fs.readFileSync(source)];
      } else {
        buffers = [fs.readFileSync(source)];
      }
    } else {
      // Buffer
      isPdf = source[0] === 0x25 && source[1] === 0x50 && source[2] === 0x44 && source[3] === 0x46;
      buffers = [source];
    }
    
    if (isPdf) {
      yield* processPdf(engine, buffers[0], options);
    } else {
      yield* processImages(engine, buffers, options);
    }
  } finally {
    if (engineCreated) {
      await engine.close();
    }
  }
}

// Export facade + document capabilities
module.exports = {
  ...facade,
  hasPdfSupport,
  recognizeDocument
};
