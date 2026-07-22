'use strict';

const path = require('node:path');
const { createModelFacade } = require('@arcships/light-ocr-runtime/facade');

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
