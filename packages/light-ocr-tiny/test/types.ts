import {
  createEngine,
  modelProfile,
  type BuiltInModel,
  type CreateEngineOptions,
} from '@arcships/light-ocr-tiny';

const model: BuiltInModel = 'ppocrv6-tiny';
const options: CreateEngineOptions = { model };
const tier: 'tiny' = modelProfile.tier;
const excluded: readonly ['ja'] = modelProfile.excludedLanguages;

void createEngine(options);
void tier;
void excluded;
