'use strict';

const { createModelFacade } = require('@arcships/light-ocr-runtime/facade');

module.exports = createModelFacade({
  model: 'ppocrv6-small',
  modelPackage: '@arcships/light-ocr-model-ppocrv6-small',
  compatibleBundleIds: [
    'ppocrv6-small-onnx-20260714.2',
    'ppocrv6-small-apple-20260715.1',
    'ppocrv6-small-webgpu-20260719.1',
    'ppocrv6-small-native-20260719.1',
  ],
  appleBundleIds: [
    'ppocrv6-small-apple-20260715.1',
    'ppocrv6-small-native-20260719.1',
  ],
  profile: {
    tier: 'small',
    model: 'ppocrv6-small',
    bundleId: 'ppocrv6-small-native-20260719.1',
    languages: 50,
    excludedLanguages: [],
    dictionaryEntries: 18709,
    maturity: 'stable',
  },
});
