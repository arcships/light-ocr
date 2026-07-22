import {
  createEngine,
  modelProfile,
  type BuiltInModel,
  type CreateEngineOptions,
} from '@arcships/light-ocr-medium';

const model: BuiltInModel = 'ppocrv6-medium';
const options: CreateEngineOptions = { model };
const tier: 'medium' = modelProfile.tier;

void createEngine(options);
void tier;
