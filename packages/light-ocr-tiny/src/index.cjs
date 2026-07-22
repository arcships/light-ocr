'use strict';

const path = require('node:path');
const { createModelFacade } = require('@arcships/light-ocr-runtime/facade');

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
