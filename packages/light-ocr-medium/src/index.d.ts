export * from '@arcships/light-ocr-runtime';

import type {
  CreateEngineOptions as RuntimeCreateEngineOptions,
  ModelProfile,
  OcrEngine,
} from '@arcships/light-ocr-runtime';

export type BuiltInModel = 'ppocrv6-medium';

export type CreateEngineOptions = Omit<RuntimeCreateEngineOptions, 'bundlePath'> & {
  readonly model?: BuiltInModel;
  readonly bundlePath?: string;
};

export function createEngine(options?: CreateEngineOptions): Promise<OcrEngine>;
export const modelProfile: ModelProfile & {
  readonly tier: 'medium';
  readonly model: BuiltInModel;
  readonly maturity: 'preview';
};
