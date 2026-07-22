export * from '@arcships/light-ocr-runtime';

import type {
  CreateEngineOptions as RuntimeCreateEngineOptions,
  OcrEngine,
} from '@arcships/light-ocr-runtime';

export type BuiltInModel = 'ppocrv6-small';

export type CreateEngineOptions = Omit<RuntimeCreateEngineOptions, 'bundlePath'> & {
  readonly model?: BuiltInModel;
  readonly bundlePath?: string;
};

export function createEngine(options?: CreateEngineOptions): Promise<OcrEngine>;
