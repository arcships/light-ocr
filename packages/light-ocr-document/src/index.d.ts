/// <reference types="node" />

import type { OcrEngine, OcrResult, RecognizeOptions, OcrError } from '@arcships/light-ocr-runtime';

// Re-export runtime types for convenience
export type { OcrEngine, OcrResult, RecognizeOptions, OcrError };

export interface Point { readonly x: number; readonly y: number }
export interface Rect {
  readonly x: number;
  readonly y: number;
  readonly width: number;
  readonly height: number;
}

export interface DocumentPage {
  readonly index: number;
  readonly width: number;
  readonly height: number;
  readonly coordinateSpace: 'pageSpace';
  readonly structure: 'ocr-order';
  readonly lines: ReadonlyArray<DocumentLine>;
  readonly source: PageSource;
  readonly timingUs: PageTimingUs;
}

export interface DocumentLine {
  readonly id: string;
  readonly text: string;
  readonly confidence: number;
  readonly box: readonly [Point, Point, Point, Point];
}

export interface PageSource {
  readonly kind: 'image' | 'pdf';
  readonly mediaType: string;
  readonly identity: Record<string, unknown>;
  readonly appliedTransforms: AppliedTransforms;
}

export interface AppliedTransforms {
  readonly exif?: ExifTransform;
  readonly pdf?: PdfTransform;
}

export interface ExifTransform {
  readonly orientation: number;
  readonly applied: boolean;
}

export interface PdfTransform {
  readonly rotation: number;
  readonly mediaBox: Rect;
  readonly cropBox: Rect;
  readonly dpi: number;
  readonly scale: number;
}

export interface PageTimingUs {
  readonly total: number;
  readonly decode: number;
  readonly ocr: number;
}

export interface DocumentResult {
  readonly schemaVersion: 1;
  readonly source: DocumentSource;
  readonly pages: ReadonlyArray<DocumentPage>;
}

export interface DocumentSource {
  readonly kind: 'image' | 'pdf' | 'page-images';
  readonly mediaType: string;
  readonly identity: Record<string, unknown>;
  readonly pageCount: number;
}

export type OutputFormat = 'json' | 'jsonl' | 'text' | 'markdown';

export interface DocumentOptions {
  /** Output format. Default: 'json' */
  readonly format?: OutputFormat;
  
  /** Page range to process (1-indexed). Default: all pages */
  readonly pageRange?: { start: number; end: number };
  
  /** PDF raster DPI. Default: 150 */
  readonly dpi?: number;
  
  /** Maximum file size in bytes. Default: 100MB */
  readonly maxFileBytes?: number;
  
  /** Maximum number of pages to process. Default: 100 */
  readonly maxPages?: number;
  
  /** Maximum pixels per page. Default: 4096*4096 */
  readonly maxPagePixels?: number;
  
  /** Maximum total pixels across all pages. Default: 100MP */
  readonly maxTotalPixels?: number;
  
  /** Abort signal for cancellation */
  readonly signal?: AbortSignal;
  
  /** OCR options passed to the engine */
  readonly ocrOptions?: RecognizeOptions;
}

export interface DocumentEngine {
  /** Process a PDF file or buffer */
  recognizePdf(source: string | Uint8Array, options?: DocumentOptions): AsyncGenerator<DocumentPage>;
  
  /** Process multiple image files or buffers */
  recognizeImages(sources: ReadonlyArray<string | Uint8Array>, options?: DocumentOptions): AsyncGenerator<DocumentPage>;
  
  /** Process any supported document source */
  recognizeDocument(source: string | Uint8Array | ReadonlyArray<string | Uint8Array>, options?: DocumentOptions): AsyncGenerator<DocumentPage>;
  
  /** Close the engine and release resources */
  close(): Promise<void>;
}

export interface CreateDocumentEngineOptions {
  /** Path to the model bundle. If not provided, uses the default from the peer dependency */
  readonly bundlePath?: string;
  
  /** OCR engine instance to reuse. If not provided, creates a new one */
  readonly engine?: OcrEngine;
  
  /** PDFium options */
  readonly pdfium?: {
    /** Maximum memory for PDFium in bytes. Default: 512MB */
    readonly maxMemory?: number;
  };
}

/** Create a document processing engine */
export function createDocumentEngine(options?: CreateDocumentEngineOptions): Promise<DocumentEngine>;

/** Get the version of this package */
export function getVersion(): string;

/** Check if PDF support is available */
export function hasPdfSupport(): boolean;
