/// <reference types="node" />

export type PixelFormat = 'gray8' | 'rgb8' | 'bgr8' | 'rgba8';
export type DetectionStrategy = 'bounded' | 'tiled' | 'upstreamExact';
export type BuiltInModel = 'ppocrv6-small';
export type ExecutionProvider = 'auto' | 'cpu' | 'apple' | 'webgpu';
export type SessionFallback = 'error' | 'cpu';
export type CpuPartition = 'allow' | 'forbid';
export type PerformanceHint = 'latency' | 'throughput';
/** WebGPU accepts auto/fp32; fp16 is reserved for the Apple provider. */
export type Precision = 'auto' | 'fp32' | 'fp16';

export interface DetectionOptions {
  readonly strategy?: DetectionStrategy;
  readonly maxSide?: number;
}

export interface ExecutionOptions {
  /** Only providers shipped and qualified by this release appear in this union. */
  readonly provider?: ExecutionProvider;
  readonly sessionFallback?: SessionFallback;
  readonly cpuPartition?: CpuPartition;
  readonly deviceId?: number;
  readonly performanceHint?: PerformanceHint;
  readonly precision?: Precision;
}

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
  readonly maxDetectionTiles: number;
  readonly maxRecognitionBatchSize: number;
  readonly maxRecognitionWidth: number;
  readonly maxTemporaryBytes: number;
}

export interface CreateEngineOptions {
  readonly model?: BuiltInModel;
  readonly bundlePath?: string;
  readonly intraOpThreads?: number;
  readonly interOpThreads?: number;
  readonly recognitionScoreThreshold?: number;
  readonly recognitionBatchSize?: number;
  readonly reducedLimits?: Omit<ResourceLimits, 'maxDetectionTiles'> & {
    /** Omission preserves the 0.1 reducedLimits source shape. */
    readonly maxDetectionTiles?: number;
  };
  readonly queueCapacity?: number;
  readonly maxPendingInputBytes?: number;
  readonly detection?: DetectionOptions;
  readonly execution?: ExecutionOptions;
}

export interface RecognizeOptions {
  readonly recognitionScoreThreshold?: number;
  readonly recognitionBatchSize?: number;
  readonly includeDiagnostics?: boolean;
  readonly signal?: AbortSignal;
  readonly useTextlineOrientation?: boolean;
  readonly detectionMaxSide?: number;
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
export interface RecognitionBatchShape {
  readonly batchSize: number;
  readonly height: number;
  readonly width: number;
  readonly computeUnit: 'cpu' | 'ane' | 'gpu';
  readonly modelId: string;
  readonly shapeBucket: string;
}
export interface DetectionPassShape {
  readonly tileOrdinal: number;
  readonly x: number;
  readonly y: number;
  readonly width: number;
  readonly height: number;
  readonly tensorWidth: number;
  readonly tensorHeight: number;
  readonly contourCandidates: number;
  readonly rawCandidates: number;
}
export interface Diagnostics {
  readonly rejectedLines: readonly RejectedLine[];
  readonly warnings: readonly DiagnosticWarning[];
  readonly detectedCandidates: number;
  readonly acceptedBoxes: number;
  readonly detectionInputWidth: number;
  readonly detectionInputHeight: number;
  readonly rawDetectionBoxes: number;
  readonly suppressedDuplicateBoxes: number;
  readonly maxLiveDetectionPassBuffers: number;
  readonly detectionPasses: readonly DetectionPassShape[];
  readonly recognitionBatchShapes: readonly RecognitionBatchShape[];
}
export interface TimingUs {
  readonly total: number;
  readonly decode: number;
  readonly inputValidation: number;
  readonly detectionPreprocess: number;
  readonly detectionInference: number;
  readonly detectionPostprocess: number;
  readonly detectionMerge: number;
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
export interface TiledDetectionInfo {
  readonly contractVersion: 'tiled-v1';
  readonly tileSide: 1280;
  readonly minimumOverlap: 128;
  readonly artificialBoundaryMargin: 32;
  readonly mergeIouThreshold: 0.5;
  readonly mergeIosThreshold: 0.8;
}
export interface ProviderCapabilityInfo {
  readonly provider: string;
  readonly packageIncluded: boolean;
  readonly deviceAvailable: boolean;
  /** True only when this exact hardware family has reviewed qualification evidence. */
  readonly deviceValidated: boolean;
}
export interface SessionExecutionInfo {
  readonly requestedProvider: string;
  readonly actualProviderChain: readonly string[];
  readonly device: string;
  readonly deviceFamily: string;
  readonly operatingSystem: string;
  readonly precision: string;
  readonly shapePolicy: string;
  readonly modelId: string;
  readonly modelSha256: string;
  readonly runtime: string;
  readonly runtimeVersion: string;
  readonly providerVersion: string;
  readonly modelCacheStatus: string;
  readonly qualificationId: string;
  /** False means the open macOS compatibility path is experimental on this device. */
  readonly deviceValidated: boolean;
  readonly sessionFallback: boolean;
  readonly fallbackReason?: string;
}
export type CreationReason =
  | 'adapter_unavailable' | 'model_compute_unsupported'
  | 'device_memory_insufficient' | 'driver_version_unsupported'
  | 'package_corrupt' | 'artifact_hash_mismatch' | 'provider_abi_mismatch'
  | 'internal_assertion_failed' | 'unrecoverable_load_failed';
export type CreationAttemptStatus = 'selected' | 'skipped' | 'fatal';
export interface CreationAttempt {
  readonly provider: string;
  readonly status: CreationAttemptStatus;
  readonly creationReason?: CreationReason;
  readonly errorCode?: CoreErrorCode;
}
export interface CreationTrace {
  readonly requestedProvider: string;
  readonly policyId?: string;
  readonly policyVersion?: number;
  readonly orderedCandidates: readonly string[];
  readonly attempts: readonly CreationAttempt[];
  readonly selectedProvider?: string;
}
export interface ExecutionInfo {
  readonly requestedProvider: ExecutionProvider;
  readonly sessionFallback: SessionFallback;
  readonly cpuPartition: CpuPartition;
  readonly deviceId?: number;
  readonly performanceHint: PerformanceHint;
  readonly requestedPrecision: Precision;
  readonly providerCapabilities: readonly ProviderCapabilityInfo[];
  readonly selectionTrace: CreationTrace;
  readonly sessions: {
    readonly detection: SessionExecutionInfo;
    readonly recognition: SessionExecutionInfo;
  };
}
export interface EngineInfo {
  readonly coreVersion: string;
  readonly modelBundleId: string;
  readonly modelBundleSchemaVersion: string;
  readonly normalizedConfigSchemaVersion: string;
  readonly backend: string;
  /** @deprecated Use execution.sessions for stage-specific provider details. */
  readonly executionProvider: string;
  readonly execution: ExecutionInfo;
  readonly capabilities: {
    readonly detection: boolean;
    readonly recognition: boolean;
    readonly textlineOrientation: boolean;
    readonly tiledDetection: boolean;
  };
  readonly concurrencyMode: 'serialized_reject_when_busy';
  readonly limits: ResourceLimits & { readonly maxConcurrentCalls: 1 };
  readonly intraOpThreads: number;
  readonly interOpThreads: number;
  readonly detectionStrategy: DetectionStrategy;
  readonly detectionMaxSide: number;
  readonly tiledDetection?: TiledDetectionInfo;
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
export type AdapterErrorCode =
  | 'bundle_io_failed' | 'queue_full' | 'environment_closing'
  | 'unsupported_platform' | 'package_load_failed';
export type OcrErrorCode = CoreErrorCode | AdapterErrorCode;

export class OcrError extends Error {
  constructor(code: OcrErrorCode, message: string, detail?: string);
  readonly name: 'OcrError';
  readonly code: OcrErrorCode;
  readonly detail?: string;
  readonly creationTrace?: CreationTrace;
}

export interface OcrEngine {
  readonly info: EngineInfo;
  recognize(image: RawImage, options?: RecognizeOptions): Promise<OcrResult>;
  recognizeEncoded(data: Uint8Array, options?: RecognizeOptions): Promise<OcrResult>;
  close(): Promise<void>;
}

export function createEngine(options?: CreateEngineOptions): Promise<OcrEngine>;
