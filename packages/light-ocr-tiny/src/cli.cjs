#!/usr/bin/env node
'use strict';

const path = require('node:path');

const facade = require('./index.cjs');

// Try to use workspace dependencies, fallback to local paths
let createCli, coreVersion, loadNative;
try {
  ({ createCli } = require('@arcships/light-ocr-runtime/cli'));
  ({ coreVersion } = require('@arcships/light-ocr-runtime/metadata'));
} catch {
  // Fallback to local runtime
  ({ createCli } = require(path.join(__dirname, '..', '..', 'runtime', 'src', 'cli.cjs')));
  ({ coreVersion } = require(path.join(__dirname, '..', '..', 'runtime', 'src', 'metadata.cjs')));
}
try {
  ({ loadNative } = require('@arcships/light-ocr-runtime'));
} catch {
  try {
    ({ loadNative } = require(path.join(__dirname, '..', '..', 'runtime', 'src', 'load-native.cjs')));
  } catch {
    // loadNative unavailable — doctor will report native as unavailable
  }
}

const packageMetadata = require('../package.json');

const cli = createCli({
  ...facade,
  commandName: 'light-ocr-tiny',
  packageVersion: packageMetadata.version,
  coreVersion,
  loadNative,
});

if (require.main === module) {
  cli.main(process.argv.slice(2)).then((code) => {
    if (code !== cli.EXIT.success) process.exitCode = code;
  });
}

module.exports = cli;
