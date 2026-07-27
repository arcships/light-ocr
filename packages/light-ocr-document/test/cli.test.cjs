'use strict';

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { main, parseArgs, USAGE } = require('../src/cli.cjs');

function capture() {
  let value = '';
  return {
    stream: {
      write(chunk) {
        value += chunk;
      },
    },
    read() {
      return value;
    },
  };
}

describe('light-ocr-document CLI', () => {
  it('parses implicit recognize sources and bounded options', () => {
    assert.deepEqual(
      parseArgs([
        'report.pdf',
        '--format', 'jsonl',
        '--pages', '2-4',
        '--dpi', '200',
        '--provider', 'cpu',
        '--quiet',
      ]),
      {
        documentOptions: {
          pageRange: { start: 2, end: 4 },
          dpi: 200,
        },
        format: 'jsonl',
        provider: 'cpu',
        quiet: true,
        sources: ['report.pdf'],
      },
    );
  });

  it('rejects unknown options, invalid ranges, and missing sources', () => {
    assert.throws(() => parseArgs(['--wat']), /unknown option/);
    assert.throws(() => parseArgs(['x.pdf', '--pages', '4-2']), /must not precede/);
    assert.throws(() => parseArgs(['--dpi', '150']), /source is required/);
  });

  it('prints help, version, and machine-readable info without exiting the process', async () => {
    const stdout = capture();
    const stderr = capture();
    assert.equal(await main(['help'], { stdout: stdout.stream, stderr: stderr.stream }), 0);
    assert.equal(stdout.read().trim(), USAGE);

    const versionOut = capture();
    assert.equal(await main(['--version'], {
      stdout: versionOut.stream,
      stderr: stderr.stream,
    }), 0);
    assert.match(versionOut.read(), /^\d+\.\d+\.\d+\n$/);

    const infoOut = capture();
    assert.equal(await main(['info'], { stdout: infoOut.stream, stderr: stderr.stream }), 0);
    const info = JSON.parse(infoOut.read());
    assert.equal(info.name, '@arcships/light-ocr');
    assert.equal(typeof info.pdfSupport, 'boolean');
  });

  it('maps usage failures to EX_DATAERR', async () => {
    const stdout = capture();
    const stderr = capture();
    assert.equal(await main(['--wat'], { stdout: stdout.stream, stderr: stderr.stream }), 65);
    assert.match(stderr.read(), /^invalid_argument:/);
  });
});
