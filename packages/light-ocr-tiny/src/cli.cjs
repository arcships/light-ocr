#!/usr/bin/env node
'use strict';

const facade = require('./index.cjs');
const { createCli } = require('@arcships/light-ocr-runtime/cli');
const { coreVersion } = require('@arcships/light-ocr-runtime/metadata');
const packageMetadata = require('../package.json');

const cli = createCli({
  ...facade,
  commandName: 'light-ocr-tiny',
  packageVersion: packageMetadata.version,
  coreVersion,
});

if (require.main === module) {
  cli.main(process.argv.slice(2)).then((code) => {
    if (code !== cli.EXIT.success) process.exitCode = code;
  });
}

module.exports = cli;
