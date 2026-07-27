export * from '@arcships/light-ocr-runtime';

import type {
  CreateEngineOptions as RuntimeCreateEngineOptions,
  ModelProfile,
  OcrEngine,
} from '@arcships/light-ocr-runtime';

export type BuiltInModel = 'ppocrv6-small';

export type CreateEngineOptions = Omit<RuntimeCreateEngineOptions, 'bundlePath'> & {
  readonly model?: BuiltInModel;
  readonly bundlePath?: string;
};

export function createEngine(options?: CreateEngineOptions): Promise<OcrEngine>;
export const modelProfile: ModelProfile & {
  readonly tier: 'small';
  readonly model: BuiltInModel;
  readonly maturity: 'stable';
};

export interface Point {
  readonly x: number;
  readonly y: number;
}

export interface Rect {
  readonly x: number;
  readonly y: number;
  readonly width: number;
  readonly height: number;
}

export interface DocumentLine {
  readonly id: string;
  readonly text: string;
  readonly confidence: number;
  readonly box: readonly [Point, Point, Point, Point];
}

export interface PdfTransform {
  readonly rotation: number;
  readonly mediaBox: Rect;
  readonly cropBox: Rect;
  readonly dpi: number;
  readonly scale: number;
}

export interface PageSource {
  readonly kind: 'image' | 'pdf';
  readonly mediaType: string;
  readonly identity: Readonly<Record<string, unknown>>;
  readonly appliedTransforms: {
    readonly pdf?: PdfTransform;
  };
}

export interface DocumentPage {
  readonly index: number;
  readonly width: number;
  readonly height: number;
  readonly coordinateSpace: 'pageSpace';
  readonly structure: 'ocr-order';
  readonly lines: ReadonlyArray<DocumentLine>;
  readonly source: PageSource;
  readonly timingUs: {
    readonly total: number;
    readonly decode: number;
    readonly ocr: number;
  };
  readonly modelBundleId?: string;
}

export type DocumentInput = string | Uint8Array;

export interface DocumentOptions {
  /** Inclusive, one-based PDF page range. */
  readonly pageRange?: { readonly start: number; readonly end: number };
  /** PDF raster resolution. Must be an integer from 36 through 600. Default: 150. */
  readonly dpi?: number;
  /** Maximum bytes accepted for each input. Default: 100 MiB. */
  readonly maxFileBytes?: number;
  /** Maximum number of pages. Default: 100. */
  readonly maxPages?: number;
  /** Maximum rendered pixels for one page. Default: 4096 × 4096. */
  readonly maxPagePixels?: number;
  /** Maximum rendered pixels across the request. Default: 100 Mi pixels. */
  readonly maxTotalPixels?: number;
  readonly signal?: AbortSignal;
  readonly ocrOptions?: import('@arcships/light-ocr-runtime').RecognizeOptions;
}

export interface DocumentEngine {
  recognizePdf(
    source: DocumentInput,
    options?: DocumentOptions,
  ): AsyncGenerator<DocumentPage>;
  recognizeImages(
    sources: ReadonlyArray<DocumentInput>,
    options?: DocumentOptions,
  ): AsyncGenerator<DocumentPage>;
  recognizeDocument(
    source: DocumentInput | ReadonlyArray<DocumentInput>,
    options?: DocumentOptions,
  ): AsyncGenerator<DocumentPage>;
  close(): Promise<void>;
}

export interface CreateDocumentEngineOptions {
  readonly engine?: OcrEngine;
  readonly engineOptions?: CreateEngineOptions;
}

export interface RecognizeDocumentOptions extends DocumentOptions {
  readonly engine?: OcrEngine;
  readonly engineOptions?: CreateEngineOptions;
}

export function createDocumentEngine(
  options?: CreateDocumentEngineOptions,
): Promise<DocumentEngine>;

export function recognizeDocument(
  source: DocumentInput | ReadonlyArray<DocumentInput>,
  options?: RecognizeDocumentOptions,
): AsyncGenerator<DocumentPage>;

export function getVersion(): string;
export function hasPdfSupport(): boolean;
