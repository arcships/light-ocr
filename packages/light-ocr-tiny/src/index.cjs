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

module.exports = createModelFacade({
  model: 'ppocrv6-tiny',
  modelPackage: '@arcships/light-ocr-model-ppocrv6-tiny',
  compatibleBundleIds: ['ppocrv6-tiny-onnx-20260722.1'],
  developmentBundlePath: path.resolve(
    __dirname,
    '../../../models/generated/ppocrv6-tiny-onnx-20260722.1',
  ),
  profile: {
    tier: 'tiny',
    model: 'ppocrv6-tiny',
    bundleId: 'ppocrv6-tiny-onnx-20260722.1',
    languages: 49,
    excludedLanguages: ['ja'],
    dictionaryEntries: 6905,
    maturity: 'preview',
  },
});
