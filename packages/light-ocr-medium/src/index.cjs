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
  model: 'ppocrv6-medium',
  modelPackage: '@arcships/light-ocr-model-ppocrv6-medium',
  compatibleBundleIds: ['ppocrv6-medium-onnx-20260722.1'],
  developmentBundlePath: path.resolve(
    __dirname,
    '../../../models/generated/ppocrv6-medium-onnx-20260722.1',
  ),
  profile: {
    tier: 'medium',
    model: 'ppocrv6-medium',
    bundleId: 'ppocrv6-medium-onnx-20260722.1',
    languages: 50,
    excludedLanguages: [],
    dictionaryEntries: 18709,
    maturity: 'preview',
  },
});
