import {
  createEngine as createRuntimeEngine,
  type CreateEngineOptions as RuntimeCreateEngineOptions,
} from '@arcships/light-ocr-runtime';
import {
  createEngine as createSmallEngine,
  type CreateEngineOptions as SmallCreateEngineOptions,
} from '@arcships/light-ocr';

const runtimeOptions: RuntimeCreateEngineOptions = { bundlePath: '/models/custom' };
void createRuntimeEngine(runtimeOptions);

const smallOptions: SmallCreateEngineOptions = { model: 'ppocrv6-small' };
void createSmallEngine();
void createSmallEngine(smallOptions);

// @ts-expect-error The model-free runtime always requires bundlePath.
void createRuntimeEngine();

// @ts-expect-error Model aliases belong to a model facade, not the runtime.
void createRuntimeEngine({ model: 'ppocrv6-small' });
