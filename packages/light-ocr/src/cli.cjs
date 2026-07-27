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
  commandName: 'light-ocr',
  packageVersion: packageMetadata.version,
  coreVersion,
  loadNative,
});

function shouldUseDocumentCli(argv) {
  if (argv[0] === 'document') return true;
  const source = argv[0] === 'recognize' ? argv[1] : argv[0];
  return typeof source === 'string' && /\.pdf$/i.test(source);
}

async function main(argv) {
  if (shouldUseDocumentCli(argv)) {
    const selectedArgs = argv[0] === 'document' ? argv.slice(1) : argv;
    return require('./document-cli.cjs').main(selectedArgs);
  }
  const code = await cli.main(argv);
  if (
    code === cli.EXIT.success
    && argv.includes('--help')
    && !argv.some((argument) => ['recognize', 'detect', 'info', 'doctor'].includes(argument))
  ) {
    process.stdout.write(
      '\nPDF: light-ocr <document.pdf> [--pages N-M] or '
      + 'light-ocr document <source...> [options]\n',
    );
  }
  return code;
}

if (require.main === module) {
  const argv = process.argv.slice(2);
  main(argv).then((code) => {
    if (code !== cli.EXIT.success) process.exitCode = code;
  });
}

module.exports = { ...cli, main };
Object.defineProperty(module.exports, 'shouldUseDocumentCli', {
  value: shouldUseDocumentCli,
  enumerable: false,
});
