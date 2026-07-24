#!/usr/bin/env node
'use strict';

const path = require('node:path');

const facade = require('./index.cjs');

// Try to use workspace dependencies, fallback to local paths
let createCli, coreVersion;
try {
  ({ createCli } = require('@arcships/light-ocr-runtime/cli'));
  ({ coreVersion } = require('@arcships/light-ocr-runtime/metadata'));
} catch {
  // Fallback to local runtime
  ({ createCli } = require(path.join(__dirname, '..', '..', 'runtime', 'src', 'cli.cjs')));
  ({ coreVersion } = require(path.join(__dirname, '..', '..', 'runtime', 'src', 'metadata.cjs')));
}

const packageMetadata = require('../package.json');

const cli = createCli({
  ...facade,
  commandName: 'light-ocr',
  packageVersion: packageMetadata.version,
  coreVersion,
});

if (require.main === module) {
  cli.main(process.argv.slice(2)).then((code) => {
    if (code !== cli.EXIT.success) process.exitCode = code;
  });
}

module.exports = cli;
