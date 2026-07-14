/// <reference types="node" />

export type PixelFormat = 'gray8' | 'rgb8' | 'bgr8' | 'rgba8';

export interface RawImage {
  readonly data: Uint8Array;
  readonly width: number;
  readonly height: number;
  readonly stride: number;
  readonly pixelFormat: PixelFormat;
}

export interface ResourceLimits {
  readonly maxWidth: number;
  readonly maxHeight: number;
  readonly maxPixels: number;
  readonly maxDetectionSide: number;
  readonly maxDetectionCandidates: number;
  readonly maxRecognitionBatchSize: number;
  readonly maxRecognitionWidth: number;
  readonly maxTemporaryBytes: number;
}

export interface CreateEngineOptions {
  readonly bundlePath: string;
  readonly intraOpThreads?: number;
  readonly interOpThreads?: number;
  readonly recognitionScoreThreshold?: number;
  readonly recognitionBatchSize?: number;
  readonly reducedLimits?: ResourceLimits;
  readonly queueCapacity?: number;
  readonly maxPendingInputBytes?: number;
}

export interface RecognizeOptions {
  readonly recognitionScoreThreshold?: number;
  readonly recognitionBatchSize?: number;
  readonly includeDiagnostics?: boolean;
  readonly signal?: AbortSignal;
  readonly useTextlineOrientation?: boolean;
}

export interface Point { readonly x: number; readonly y: number }
export interface OcrLine {
  readonly text: string;
  readonly confidence: number;
  readonly box: readonly [Point, Point, Point, Point];
}
export type RejectionReason = 'below_score_threshold' | 'empty_decode';
export interface RejectedLine { readonly line: OcrLine; readonly reason: RejectionReason }
export interface DiagnosticWarning { readonly code: string; readonly message: string }
export interface Diagnostics {
  readonly rejectedLines: readonly RejectedLine[];
  readonly warnings: readonly DiagnosticWarning[];
  readonly detectedCandidates: number;
  readonly acceptedBoxes: number;
}
export interface TimingUs {
  readonly total: number;
  readonly inputValidation: number;
  readonly detectionPreprocess: number;
  readonly detectionInference: number;
  readonly detectionPostprocess: number;
  readonly cropAndSort: number;
  readonly recognitionPreprocess: number;
  readonly recognitionInference: number;
  readonly recognitionPostprocess: number;
}
export interface OcrResult {
  readonly lines: readonly OcrLine[];
  readonly imageWidth: number;
  readonly imageHeight: number;
  readonly modelBundleId: string;
  readonly timingUs: TimingUs;
  readonly diagnostics?: Diagnostics;
}
export interface EngineInfo {
  readonly coreVersion: string;
  readonly modelBundleId: string;
  readonly modelBundleSchemaVersion: string;
  readonly backend: string;
  readonly executionProvider: string;
  readonly capabilities: {
    readonly detection: boolean;
    readonly recognition: boolean;
    readonly textlineOrientation: boolean;
  };
  readonly concurrencyMode: 'serialized_reject_when_busy';
  readonly limits: ResourceLimits & { readonly maxConcurrentCalls: 1 };
  readonly intraOpThreads: number;
  readonly interOpThreads: number;
  readonly defaultRecognitionScoreThreshold: number;
  readonly defaultRecognitionBatchSize: number;
  readonly adapter: {
    readonly scheduler: 'dedicated_fifo';
    readonly queueCapacity: number;
    readonly maxPendingInputBytes: number;
  };
}

export type CoreErrorCode =
  | 'invalid_argument' | 'invalid_image' | 'unsupported_pixel_format'
  | 'unsupported_capability' | 'invalid_model_bundle' | 'unsupported_model'
  | 'model_integrity_failed' | 'runtime_initialization_failed' | 'inference_failed'
  | 'postprocess_failed' | 'resource_limit_exceeded' | 'invalid_engine'
  | 'internal_error';
export type AdapterErrorCode = 'bundle_io_failed' | 'queue_full' | 'environment_closing';
export type OcrErrorCode = CoreErrorCode | AdapterErrorCode;

export class OcrError extends Error {
  constructor(code: OcrErrorCode, message: string, detail?: string);
  readonly name: 'OcrError';
  readonly code: OcrErrorCode;
  readonly detail?: string;
}

export interface OcrEngine {
  readonly info: EngineInfo;
  recognize(image: RawImage, options?: RecognizeOptions): Promise<OcrResult>;
  close(): Promise<void>;
}

export function createEngine(options: CreateEngineOptions): Promise<OcrEngine>;
