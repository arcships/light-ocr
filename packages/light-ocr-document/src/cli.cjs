#!/usr/bin/env node
'use strict';

const {
  createDocumentEngine,
  getVersion,
  hasPdfSupport,
  OcrError,
} = require('./index.cjs');

const USAGE = `light-ocr-document - local PDF and multi-page image OCR (preview)

Usage:
  light-ocr-document [recognize] <source...> [options]
  light-ocr-document info
  light-ocr-document help

Options:
  --format <json|jsonl|text>  Output format (default: json)
  --pages <N|N-M>             Inclusive PDF page range
  --dpi <36-600>              PDF raster DPI (default: 150)
  --max-pages <n>             Maximum pages (default: 100)
  --max-page-pixels <n>       Maximum rendered pixels per page
  --max-total-pixels <n>      Maximum rendered pixels for the request
  --max-file-bytes <n>        Maximum bytes per input
  --provider <auto|cpu|apple|webgpu>
  --quiet                     Suppress progress output
  -h, --help                  Show help
  -v, --version               Show version`;

const EXIT_CODES = Object.freeze({
  invalid_argument: 65,
  invalid_image: 66,
  unsupported_capability: 67,
  invalid_model_bundle: 68,
  resource_limit_exceeded: 69,
  package_load_failed: 70,
  inference_failed: 71,
  internal_error: 72,
});

function argumentError(message) {
  return new OcrError('invalid_argument', message);
}

function takeValue(args, index, flag) {
  const value = args[index + 1];
  if (value === undefined || value.startsWith('-')) {
    throw argumentError(`${flag} requires a value`);
  }
  return value;
}

function parseInteger(value, flag) {
  if (!/^[1-9]\d*$/.test(value)) {
    throw argumentError(`${flag} must be a positive integer`);
  }
  const parsed = Number(value);
  if (!Number.isSafeInteger(parsed)) {
    throw argumentError(`${flag} is too large`);
  }
  return parsed;
}

function parseArgs(argv) {
  const args = [...argv];
  if (args[0] === 'recognize') args.shift();
  const sources = [];
  const documentOptions = {};
  let format = 'json';
  let provider = 'auto';
  let quiet = false;

  for (let index = 0; index < args.length; index++) {
    const arg = args[index];
    if (arg === '--quiet') {
      quiet = true;
    } else if (arg === '--format') {
      format = takeValue(args, index, arg);
      index++;
      if (!['json', 'jsonl', 'text'].includes(format)) {
        throw argumentError('--format must be json, jsonl, or text');
      }
    } else if (arg === '--pages') {
      const value = takeValue(args, index, arg);
      index++;
      const match = /^([1-9]\d*)(?:-([1-9]\d*))?$/.exec(value);
      if (!match) throw argumentError('--pages must be N or N-M');
      const start = parseInteger(match[1], '--pages');
      const end = parseInteger(match[2] ?? match[1], '--pages');
      if (end < start) throw argumentError('--pages end must not precede its start');
      documentOptions.pageRange = { start, end };
    } else if (
      [
        '--dpi',
        '--max-pages',
        '--max-page-pixels',
        '--max-total-pixels',
        '--max-file-bytes',
      ].includes(arg)
    ) {
      const value = parseInteger(takeValue(args, index, arg), arg);
      index++;
      const keys = {
        '--dpi': 'dpi',
        '--max-pages': 'maxPages',
        '--max-page-pixels': 'maxPagePixels',
        '--max-total-pixels': 'maxTotalPixels',
        '--max-file-bytes': 'maxFileBytes',
      };
      documentOptions[keys[arg]] = value;
    } else if (arg === '--provider') {
      provider = takeValue(args, index, arg);
      index++;
      if (!['auto', 'cpu', 'apple', 'webgpu'].includes(provider)) {
        throw argumentError('--provider must be auto, cpu, apple, or webgpu');
      }
    } else if (arg.startsWith('-')) {
      throw argumentError(`unknown option: ${arg}`);
    } else {
      sources.push(arg);
    }
  }
  if (sources.length === 0) throw argumentError('at least one source is required');
  return { documentOptions, format, provider, quiet, sources };
}

function writeLine(stream, value = '') {
  stream.write(`${value}\n`);
}

async function main(
  argv = process.argv.slice(2),
  io = { stdout: process.stdout, stderr: process.stderr },
) {
  if (
    argv.length === 0
    || argv[0] === 'help'
    || argv.includes('--help')
    || argv.includes('-h')
  ) {
    writeLine(io.stdout, USAGE);
    return 0;
  }
  if (argv.includes('--version') || argv.includes('-v')) {
    writeLine(io.stdout, getVersion());
    return 0;
  }
  if (argv[0] === 'info') {
    writeLine(io.stdout, JSON.stringify({
      name: '@arcships/light-ocr-document',
      version: getVersion(),
      pdfSupport: hasPdfSupport(),
    }));
    return 0;
  }

  let parsed;
  let engine;
  try {
    parsed = parseArgs(argv);
    engine = await createDocumentEngine({
      engineOptions: { execution: { provider: parsed.provider } },
    });
    const source = parsed.sources.length === 1 ? parsed.sources[0] : parsed.sources;
    const pages = parsed.format === 'json' ? [] : undefined;
    let count = 0;
    for await (const page of engine.recognizeDocument(source, parsed.documentOptions)) {
      count++;
      if (pages) pages.push(page);
      if (parsed.format === 'jsonl') writeLine(io.stdout, JSON.stringify(page));
      if (parsed.format === 'text') {
        if (count > 1) writeLine(io.stdout);
        for (const line of page.lines) writeLine(io.stdout, line.text);
      }
      if (!parsed.quiet) io.stderr.write(`\rProcessed page ${count}`);
    }
    if (!parsed.quiet) writeLine(io.stderr);
    if (pages) {
      writeLine(io.stdout, JSON.stringify({
        schemaVersion: 1,
        source: {
          kind: pages[0]?.source.kind === 'pdf' ? 'pdf' : 'page-images',
          mediaType: pages[0]?.source.mediaType ?? 'application/octet-stream',
          identity: {},
          pageCount: pages.length,
        },
        pages,
      }, null, 2));
    }
    return 0;
  } catch (error) {
    if (error?.name === 'AbortError') {
      writeLine(io.stderr, 'The operation was aborted');
      return 72;
    }
    if (error instanceof OcrError) {
      writeLine(io.stderr, `${error.code}: ${error.message}`);
      return EXIT_CODES[error.code] ?? 72;
    }
    writeLine(io.stderr, `internal_error: ${error?.message ?? String(error)}`);
    return 72;
  } finally {
    await engine?.close();
  }
}

if (require.main === module) {
  main().then(
    (code) => {
      process.exitCode = code;
    },
    (error) => {
      console.error(error);
      process.exitCode = 72;
    },
  );
}

module.exports = { main, parseArgs, USAGE };
