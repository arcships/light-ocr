'use strict';

const fs = require('node:fs');
const path = require('node:path');

const versionPath = path.resolve(__dirname, '../../../VERSION');
const coreVersion = fs.readFileSync(versionPath, 'utf8').trim();

module.exports = Object.freeze({ coreVersion });
