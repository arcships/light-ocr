#!/usr/bin/env node
'use strict';

const cli = require('@arcships/light-ocr/document-cli');

if (require.main === module) {
  cli.main(process.argv.slice(2)).then(
    (code) => {
      process.exitCode = code;
    },
    (error) => {
      console.error(error);
      process.exitCode = 72;
    },
  );
}

module.exports = cli;
