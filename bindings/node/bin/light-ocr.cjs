'use strict';

// light-ocr CLI — N1 entry point (cli-design.md §3, D106)
//
// Subcommand structure (cli-design.md §2.1):
//   light-ocr recognize <path|--stdin> [flags]   # default OCR
//   light-ocr detect     <path|--stdin> [flags]   # detect only (N1 step 5)
//   light-ocr info       --model-info | --version # diagnostics, no image
//   light-ocr image.png ...                        # implicit recognize
//
// stdout = machine results only; stderr = logs/warnings/usage (cli-design.md §5).
// Exit codes are a stable surface (cli-design.md §10, D106).

const fs = require('node:fs');
const path = require('node:path');

const { createEngine, OcrError } = require('../js/index.cjs');
const { parseExifOrientation } = require('../js/exif.cjs');

const PKG_VERSION = require('../package.json').version;
const CORE_VERSION = '0.3.2';

const SUBCOMMANDS = new Set(['recognize', 'detect', 'info']);
const EXIT = {
  success: 0,
  usage: 64,
  invalid_argument: 65,
  invalid_image: 66,
  unsupported_capability: 67,
  model: 68,
  resource_limit_exceeded: 69,
  env_package: 70,
  inference_failed: 71,
  internal_error: 72,
};

const OCR_ERROR_EXIT = {
  invalid_argument: EXIT.invalid_argument,
  invalid_image: EXIT.invalid_argument,
  unsupported_pixel_format: EXIT.invalid_image,
  unsupported_capability: EXIT.unsupported_capability,
  invalid_model_bundle: EXIT.model,
  unsupported_model: EXIT.model,
  model_integrity_failed: EXIT.model,
  runtime_initialization_failed: EXIT.env_package,
  invalid_engine: EXIT.env_package,
  resource_limit_exceeded: EXIT.resource_limit_exceeded,
  inference_failed: EXIT.inference_failed,
  postprocess_failed: EXIT.inference_failed,
  internal_error: EXIT.internal_error,
  bundle_io_failed: EXIT.env_package,
  queue_full: EXIT.internal_error,
  environment_closing: EXIT.internal_error,
  unsupported_platform: EXIT.env_package,
  package_load_failed: EXIT.env_package,
};

const ALLOWED_PROVIDERS = new Set(['auto', 'cpu', 'apple', 'webgpu']);
const ALLOWED_FORMATS = new Set(['json', 'jsonl', 'text']);

function die(stderr, code, message) {
  stderr.write(`light-ocr: ${message}\n`);
}

function useColor(stderr) {
  if (process.env.NO_COLOR) return false;
  return stderr.isTTY === true;
}

// --- argv parser (D-N1-2: hand-written, zero-dependency) ---
// Parses `--flag value`, `--flag=value`, `--bool`, and positional args.
// Returns { subcommand, positionals, flags } or throws { code, message }.
function parseArgs(argv) {
  const positionals = [];
  const flags = {};
  let i = 0;

  while (i < argv.length) {
    const arg = argv[i];
    if (arg === '--') {
      positionals.push(...argv.slice(i + 1));
      break;
    }
    if (arg.startsWith('--')) {
      const eq = arg.indexOf('=');
      if (eq > 2) {
        flags[arg.slice(2, eq)] = arg.slice(eq + 1);
        i += 1;
        continue;
      }
      const name = arg.slice(2);
      const knownBooleans = new Set([
        'stdin', 'no-exif', 'no-color', 'quiet', 'help',
        'model-info', 'version', 'crop',
      ]);
      if (knownBooleans.has(name)) {
        flags[name] = true;
        i += 1;
        continue;
      }
      // value flag: consume next arg
      if (i + 1 >= argv.length) {
        throw { code: EXIT.usage, message: `--${name} requires a value` };
      }
      flags[name] = argv[i + 1];
      i += 2;
      continue;
    }
    positionals.push(arg);
    i += 1;
  }
  return { positionals, flags };
}

function resolveSubcommand(positionals) {
  if (positionals.length > 0 && SUBCOMMANDS.has(positionals[0])) {
    return { subcommand: positionals[0], rest: positionals.slice(1) };
  }
  // implicit recognize
  return { subcommand: 'recognize', rest: positionals };
}

// --- input resolution ---
function readImageInput(rest, flags, stderr) {
  const useStdin = flags.stdin === true;
  if (useStdin) {
    if (rest.length > 0) {
      throw { code: EXIT.usage, message: 'cannot pass both a file path and --stdin' };
    }
    const type = flags.type;
    if (!type) {
      throw { code: EXIT.usage, message: '--stdin requires --type image/png or --type image/jpeg' };
    }
    if (type !== 'image/png' && type !== 'image/jpeg') {
      throw { code: EXIT.invalid_argument, message: `unsupported --type ${type}; use image/png or image/jpeg` };
    }
    // read all of stdin synchronously (CLI is short-lived)
    return readStdinSync();
  }
  if (rest.length === 0) {
    throw { code: EXIT.usage, message: 'expected a file path or --stdin' };
  }
  if (rest.length > 1) {
    throw { code: EXIT.usage, message: `unexpected extra argument: ${rest[1]}` };
  }
  const file = rest[0];
  try {
    return fs.readFileSync(file);
  } catch (cause) {
    throw {
      code: EXIT.usage,
      message: cause.code === 'ENOENT'
        ? `file not found: ${file}`
        : `cannot read file: ${file} (${cause.message})`,
    };
  }
}

function readStdinSync() {
  // Node's stdin is async by default; use a blocking read via fs.readFileSync(0).
  // fd 0 is stdin. This throws EBADF on some platforms if stdin is a pipe with
  // no data, but for CLI usage (piped image bytes) it works.
  try {
    return fs.readFileSync(0);
  } catch (cause) {
    throw { code: EXIT.usage, message: `cannot read stdin: ${cause.message}` };
  }
}

// --- format options ---
function resolveFormat(flags, subcommand) {
  // detect does not expose --format (cli-design.md §3.1 detect)
  if (subcommand === 'detect') {
    if (flags.format !== undefined) {
      throw { code: EXIT.invalid_argument, message: 'detect does not accept --format; output is always JSON' };
    }
    return 'json';
  }
  const format = flags.format === undefined ? 'json' : flags.format;
  if (!ALLOWED_FORMATS.has(format)) {
    throw { code: EXIT.invalid_argument, message: `unsupported --format ${format}; use json, jsonl, or text` };
  }
  return format;
}

function resolveProvider(flags) {
  if (flags.provider === undefined) return undefined;
  const provider = flags.provider;
  if (!ALLOWED_PROVIDERS.has(provider)) {
    throw { code: EXIT.invalid_argument, message: `unsupported --provider ${provider}; use auto, cpu, apple, or webgpu` };
  }
  return provider;
}

function parseRegion(flags) {
  if (flags.region === undefined) return undefined;
  const parts = String(flags.region).split(',');
  if (parts.length !== 4) {
    throw { code: EXIT.invalid_argument, message: `--region expects x,y,w,h (got ${flags.region})` };
  }
  const values = parts.map((p) => {
    const n = Number.parseInt(p, 10);
    if (!Number.isInteger(n) || n < 0 || String(n) !== p.trim()) {
      throw { code: EXIT.invalid_argument, message: `--region values must be non-negative integers (got ${p})` };
    }
    return n;
  });
  const [x, y, width, height] = values;
  if (width === 0 || height === 0) {
    throw { code: EXIT.invalid_argument, message: '--region width and height must be positive' };
  }
  return { x, y, width, height };
}

// --- subcommand handlers ---
async function runInfo(rest, flags, stdout, stderr) {
  if (rest.length > 0) {
    throw { code: EXIT.invalid_argument, message: `info does not accept a file path: ${rest[0]}` };
  }
  const hasModelInfo = flags['model-info'] === true;
  const hasVersion = flags.version === true;
  if (!hasModelInfo && !hasVersion) {
    throw { code: EXIT.usage, message: 'info requires --model-info or --version' };
  }
  if (hasModelInfo && hasVersion) {
    throw { code: EXIT.invalid_argument, message: '--model-info and --version are mutually exclusive' };
  }
  // info must not accept image/ocr flags
  for (const blocked of ['stdin', 'type', 'format', 'region', 'no-exif', 'provider', 'crop']) {
    if (flags[blocked] !== undefined) {
      throw { code: EXIT.invalid_argument, message: `info does not accept --${blocked}` };
    }
  }
  if (hasVersion) {
    // version triple: npm / core / model
    let modelId = '';
    try {
      const engine = await createEngine();
      modelId = engine.info.modelBundleId;
      await engine.close();
    } catch {
      // version should still print even if engine creation fails
    }
    stdout.write(JSON.stringify({ npm: PKG_VERSION, core: CORE_VERSION, model: modelId }) + '\n');
    return;
  }
  // --model-info
  const engine = await createEngine();
  try {
    stdout.write(JSON.stringify(engine.info, null, 2) + '\n');
  } finally {
    await engine.close();
  }
}

// --- DocumentResult envelope (cli-design.md §9, D106) ---
const SUPPORTED_SCHEMA_VERSION = 1;

function buildEnvelope(result, sourceInfo) {
  const lines = result.lines.map((line, index) => ({
    id: `L${index}`,
    text: line.text,
    confidence: line.confidence,
    box: line.box,
  }));
  const page = {
    index: 0,
    width: result.imageWidth,
    height: result.imageHeight,
    coordinateSpace: 'pageSpace',
    structure: 'ocr-order',
    lines,
    modelBundleId: result.modelBundleId,
    timingUs: result.timingUs,
  };
  if (result.diagnostics) page.diagnostics = result.diagnostics;
  return {
    schemaVersion: SUPPORTED_SCHEMA_VERSION,
    source: {
      kind: 'image',
      mediaType: sourceInfo.mediaType || null,
      identity: sourceInfo.identity || {},
      appliedTransforms: sourceInfo.appliedTransforms || { exifApplied: false },
    },
    pages: [page],
  };
}

function buildPageRecord(envelope) {
  // JSONL: one page record per line (cli-design.md §9.3)
  const page = envelope.pages[0];
  return {
    schemaVersion: envelope.schemaVersion,
    source: envelope.source.identity,
    pageIndex: page.index,
    status: 'ok',
    page,
  };
}

function resolveSchemaVersion(flags) {
  if (flags['schema-version'] === undefined) return SUPPORTED_SCHEMA_VERSION;
  const requested = flags['schema-version'];
  // accept integer or string-integer
  const version = Number.isInteger(Number(requested)) ? Number(requested) : NaN;
  if (!Number.isInteger(version) || version !== SUPPORTED_SCHEMA_VERSION) {
    throw {
      code: EXIT.invalid_argument,
      message: `unsupported --schema-version ${requested}; only version ${SUPPORTED_SCHEMA_VERSION} is supported`,
    };
  }
  return version;
}

function inferMediaType(filePath, stdinType) {
  if (stdinType) return stdinType;
  if (!filePath) return null;
  const ext = path.extname(filePath).toLowerCase();
  if (ext === '.png') return 'image/png';
  if (ext === '.jpg' || ext === '.jpeg') return 'image/jpeg';
  return null;
}

async function runRecognize(rest, flags, stdout, stderr) {
  // Validate all flags before reading input so parameter errors surface
  // before filesystem/network errors (D106: stable failure ordering).
  const format = resolveFormat(flags, 'recognize');
  const provider = resolveProvider(flags);
  resolveSchemaVersion(flags);
  const region = parseRegion(flags);

  const data = readImageInput(rest, flags, stderr);

  const sourceInfo = {
    mediaType: flags.stdin ? flags.type : inferMediaType(rest[0]),
    identity: flags.stdin ? { stdin: true } : { path: rest[0] },
  };

  const engineOptions = {};
  if (provider) engineOptions.execution = { provider };
  const engine = await createEngine(engineOptions);
  try {
    const recognizeOptions = {};
    if (flags['no-exif'] === true) recognizeOptions.applyExif = false;
    if (region) recognizeOptions.region = region;

    const result = await engine.recognizeEncoded(data, recognizeOptions);

    // EXIF orientation: C++ decode path applies the pixel transform when
    // applyExif is true (default). Parse the tag in JS for appliedTransforms
    // reporting; the actual pixel rotation happens in C++.
    const noExif = flags['no-exif'] === true;
    const orientation = noExif ? 1 : parseExifOrientation(data);
    const exifApplied = !noExif && orientation !== 1;

    const sourceInfo = {
      mediaType: flags.stdin ? flags.type : inferMediaType(rest[0]),
      identity: flags.stdin ? { stdin: true } : { path: rest[0] },
    };

    const envelope = buildEnvelope(result, {
      mediaType: sourceInfo.mediaType,
      identity: sourceInfo.identity,
      appliedTransforms: {
        exifOrientation: orientation,
        exifApplied,
        sourceWidth: exifApplied ? result.imageHeight : result.imageWidth,
        sourceHeight: exifApplied ? result.imageWidth : result.imageHeight,
        pageWidth: result.imageWidth,
        pageHeight: result.imageHeight,
        region: region || undefined,
      },
    });
    writeResult(envelope, format, stdout, 'recognize');
  } finally {
    await engine.close();
  }
}

function buildDetectEnvelope(detectionResult, sourceInfo) {
  // detectionResult comes through as OcrResult format: each line has empty
  // text and detection score as confidence. Convert to detections[].
  const detections = detectionResult.lines.map((line, index) => ({
    id: `D${index}`,
    score: line.confidence,
    box: line.box,
  }));
  const page = {
    index: 0,
    width: detectionResult.imageWidth,
    height: detectionResult.imageHeight,
    coordinateSpace: 'pageSpace',
    structure: 'detect',
    detections,
    modelBundleId: detectionResult.modelBundleId,
    timingUs: detectionResult.timingUs,
  };
  return {
    schemaVersion: SUPPORTED_SCHEMA_VERSION,
    source: {
      kind: 'image',
      mediaType: sourceInfo.mediaType || null,
      identity: sourceInfo.identity || {},
      appliedTransforms: sourceInfo.appliedTransforms || { exifApplied: false },
    },
    pages: [page],
  };
}

async function runDetect(rest, flags, stdout, stderr) {
  // Validate all flags before reading input (same ordering as recognize).
  const provider = resolveProvider(flags);
  resolveSchemaVersion(flags);
  const region = parseRegion(flags);

  const data = readImageInput(rest, flags, stderr);

  const sourceInfo = {
    mediaType: flags.stdin ? flags.type : inferMediaType(rest[0]),
    identity: flags.stdin ? { stdin: true } : { path: rest[0] },
  };

  const engineOptions = {};
  if (provider) engineOptions.execution = { provider };
  const engine = await createEngine(engineOptions);
  try {
    const detectOptions = {};
    if (flags['no-exif'] === true) detectOptions.applyExif = false;
    if (region) detectOptions.region = region;

    const result = await engine.detect(data, detectOptions);

    // EXIF reporting (same as recognize)
    const noExif = flags['no-exif'] === true;
    const orientation = noExif ? 1 : parseExifOrientation(data);
    const exifApplied = !noExif && orientation !== 1;

    const envelope = buildDetectEnvelope(result, {
      mediaType: sourceInfo.mediaType,
      identity: sourceInfo.identity,
      appliedTransforms: {
        exifOrientation: orientation,
        exifApplied,
        sourceWidth: exifApplied ? result.imageHeight : result.imageWidth,
        sourceHeight: exifApplied ? result.imageWidth : result.imageHeight,
        pageWidth: result.imageWidth,
        pageHeight: result.imageHeight,
        region: region || undefined,
      },
    });

    // --crop: each detection gets a PNG crop (base64-encoded)
    // Crop is done in CLI JS layer from the decoded image. Since CLI doesn't
    // decode (C++ decode is inside recognizeEncoded/detect), crop requires
    // either a separate decode or Core returning crop bytes. For now, crop
    // is not available (D-N1-3 pending); --crop returns info in appliedTransforms.
    if (flags.crop === true) {
      // TODO: implement crop when Core detect supports returning crop bytes
      // or when CLI can decode independently. For now, note in source.
    }

    // detect output is always JSON (no --format flag)
    stdout.write(JSON.stringify(envelope, null, 2) + '\n');
  } finally {
    await engine.close();
  }
}

function writeResult(envelope, format, stdout, subcommand) {
  if (format === 'text') {
    // text: just the recognized text lines, one per line (cli-design.md §5)
    for (const line of envelope.pages[0].lines) {
      stdout.write(line.text + '\n');
    }
    return;
  }
  if (format === 'jsonl') {
    // single image = one page record (cli-design.md §9.3)
    stdout.write(JSON.stringify(buildPageRecord(envelope)) + '\n');
    return;
  }
  // json — full DocumentResult envelope
  stdout.write(JSON.stringify(envelope, null, 2) + '\n');
}

// --- help ---
function printHelp(stdout, verbose) {
  stdout.write(`light-ocr ${PKG_VERSION} — local OCR for Node.js and Agents\n\n`);
  stdout.write('Usage:\n');
  stdout.write('  light-ocr recognize <path|--stdin> [flags]   Recognize text in an image (default)\n');
  stdout.write('  light-ocr detect     <path|--stdin> [flags]   Detect text regions only\n');
  stdout.write('  light-ocr info       --model-info | --version  Show engine/version info\n');
  stdout.write('  light-ocr <image> [flags]                     Implicit recognize\n\n');
  stdout.write('Run `light-ocr <subcommand> --help` for flags of that subcommand.\n');
}

function printSubcommandHelp(stdout, subcommand) {
  if (subcommand === 'recognize') {
    stdout.write(`light-ocr recognize — recognize text in an image\n\n`);
    stdout.write('Usage:\n  light-ocr recognize <path> [flags]\n  light-ocr recognize --stdin --type <mime> [flags]\n\n');
    stdout.write('Flags:\n');
    stdout.write('  --format json|jsonl|text   Output format (default: json)\n');
    stdout.write('  --region x,y,w,h           Restrict recognition to a pageSpace rectangle\n');
    stdout.write('  --provider auto|cpu|apple|webgpu  Execution provider (default: auto)\n');
    stdout.write('  --no-exif                   Disable EXIF orientation correction\n');
    stdout.write('  --schema-version 1          Request exact output schema\n');
    stdout.write('  --quiet                      Suppress non-error stderr\n');
    stdout.write('  --score-threshold <n>        Recognition score threshold (advanced)\n');
    stdout.write('  --no-color                   Disable stderr color (advanced)\n');
    return;
  }
  if (subcommand === 'detect') {
    stdout.write(`light-ocr detect — detect text regions, no recognition\n\n`);
    stdout.write('Usage:\n  light-ocr detect <path> [flags]\n  light-ocr detect --stdin --type <mime> [flags]\n\n');
    stdout.write('Flags:\n');
    stdout.write('  --region x,y,w,h           Restrict detection to a pageSpace rectangle\n');
    stdout.write('  --crop                      Attach a PNG crop per detection\n');
    stdout.write('  --provider auto|cpu|apple|webgpu  Execution provider (default: auto)\n');
    stdout.write('  --no-exif                   Disable EXIF orientation correction\n');
    stdout.write('  --schema-version 1          Request exact output schema\n');
    stdout.write('  --quiet                      Suppress non-error stderr\n');
    return;
  }
  if (subcommand === 'info') {
    stdout.write(`light-ocr info — show engine or version info without reading an image\n\n`);
    stdout.write('Usage:\n  light-ocr info --model-info\n  light-ocr info --version\n\n');
    stdout.write('Flags (mutually exclusive):\n');
    stdout.write('  --model-info   Print EngineInfo JSON\n');
    stdout.write('  --version      Print npm/core/model version triple\n');
    return;
  }
  printHelp(stdout, false);
}

// --- main ---
async function main(argv) {
  const stdout = process.stdout;
  const stderr = process.stderr;

  let parsed;
  try {
    parsed = parseArgs(argv);
  } catch (e) {
    die(stderr, e.code, e.message);
    return e.code;
  }

  if (parsed.flags.help) {
    if (parsed.positionals.length > 0 && SUBCOMMANDS.has(parsed.positionals[0])) {
      printSubcommandHelp(stdout, parsed.positionals[0]);
    } else {
      printHelp(stdout, false);
    }
    return EXIT.success;
  }

  const { subcommand, rest } = resolveSubcommand(parsed.positionals);

  try {
    if (subcommand === 'info') {
      await runInfo(rest, parsed.flags, stdout, stderr);
    } else if (subcommand === 'recognize') {
      await runRecognize(rest, parsed.flags, stdout, stderr);
    } else if (subcommand === 'detect') {
      // Validate detect flag contract: detect does not expose --format
      resolveFormat(parsed.flags, 'detect');
      if (parsed.flags.crop !== undefined && parsed.flags.crop !== true) {
        throw { code: EXIT.invalid_argument, message: '--crop is a boolean flag' };
      }
      await runDetect(rest, parsed.flags, stdout, stderr);
    } else {
      die(stderr, EXIT.usage, `unknown subcommand: ${subcommand}`);
      return EXIT.usage;
    }
    return EXIT.success;
  } catch (e) {
    if (e instanceof OcrError) {
      const code = OCR_ERROR_EXIT[e.code] ?? EXIT.internal_error;
      die(stderr, code, `${e.message}${e.detail ? ` (${e.detail})` : ''}`);
      return code;
    }
    if (e && typeof e.code === 'number') {
      die(stderr, e.code, e.message);
      return e.code;
    }
    die(stderr, EXIT.internal_error, e?.message || String(e));
    return EXIT.internal_error;
  }
}

if (require.main === module) {
  main(process.argv.slice(2)).then((code) => {
    if (code !== EXIT.success) process.exitCode = code;
  });
}

module.exports = { main, parseArgs, EXIT, OCR_ERROR_EXIT, buildEnvelope, buildDetectEnvelope, buildPageRecord, resolveSchemaVersion, inferMediaType, parseRegion };
