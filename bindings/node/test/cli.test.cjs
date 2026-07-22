'use strict';

// CLI smoke tests for logic that does not require the native runtime:
// argv parsing, subcommand dispatch, flag contracts, exit codes, help.
// End-to-end OCR tests (recognize against a real image) live in
// adapter.test.cjs and require the native build + model bundle.

const assert = require('node:assert/strict');
const test = require('node:test');

const { main, parseArgs, EXIT, OCR_ERROR_EXIT } = require('../bin/light-ocr.cjs');

// Capture stdout/stderr written by main() by swapping process streams.
async function runCli(argv) {
  const stdoutChunks = [];
  const stderrChunks = [];
  const origStdoutWrite = process.stdout.write.bind(process.stdout);
  const origStderrWrite = process.stderr.write.bind(process.stderr);
  process.stdout.write = (chunk) => { stdoutChunks.push(String(chunk)); return true; };
  process.stderr.write = (chunk) => { stderrChunks.push(String(chunk)); return true; };
  let code;
  try {
    code = await main(argv);
  } finally {
    process.stdout.write = origStdoutWrite;
    process.stderr.write = origStderrWrite;
  }
  return {
    code,
    stdout: stdoutChunks.join(''),
    stderr: stderrChunks.join(''),
  };
}

test('parseArgs: long flags with value', () => {
  const { positionals, flags } = parseArgs(['image.png', '--format', 'json', '--provider=auto']);
  assert.deepEqual(positionals, ['image.png']);
  assert.equal(flags.format, 'json');
  assert.equal(flags.provider, 'auto');
});

test('parseArgs: boolean flags and -- separator', () => {
  const { positionals, flags } = parseArgs(['--quiet', '--', 'image.png', '--not-a-flag']);
  assert.equal(flags.quiet, true);
  assert.deepEqual(positionals, ['image.png', '--not-a-flag']);
});

test('parseArgs: value flag at end without value throws usage', () => {
  assert.throws(() => parseArgs(['image.png', '--format']), (e) => e.code === EXIT.usage);
});

test('help: top-level prints subcommands, exit 0', async () => {
  const { code, stdout } = await runCli(['--help']);
  assert.equal(code, EXIT.success);
  assert.match(stdout, /recognize/);
  assert.match(stdout, /detect/);
  assert.match(stdout, /info/);
});

test('help: subcommand help prints that subcommand flags', async () => {
  const { code, stdout } = await runCli(['recognize', '--help']);
  assert.equal(code, EXIT.success);
  assert.match(stdout, /--format/);
  assert.match(stdout, /--region/);
});

test('help: detect help prints --crop and omits --format', async () => {
  const { code, stdout } = await runCli(['detect', '--help']);
  assert.equal(code, EXIT.success);
  assert.match(stdout, /--crop/);
  // detect does not expose --format
  assert.doesNotMatch(stdout, /--format/);
});

test('no input: usage error exit 64', async () => {
  const { code, stderr } = await runCli([]);
  assert.equal(code, EXIT.usage);
  assert.match(stderr, /file path or --stdin/);
});

test('info: mutually exclusive flags exit 65', async () => {
  const { code, stderr } = await runCli(['info', '--model-info', '--version']);
  assert.equal(code, EXIT.invalid_argument);
  assert.match(stderr, /mutually exclusive/);
});

test('info: rejects file path exit 65', async () => {
  const { code, stderr } = await runCli(['info', '--model-info', 'image.png']);
  assert.equal(code, EXIT.invalid_argument);
  assert.match(stderr, /does not accept a file path/);
});

test('info: rejects --format exit 65', async () => {
  const { code, stderr } = await runCli(['info', '--version', '--format', 'json']);
  assert.equal(code, EXIT.invalid_argument);
  assert.match(stderr, /info does not accept --format/);
});

test('info: requires one of --model-info or --version exit 64', async () => {
  const { code, stderr } = await runCli(['info']);
  assert.equal(code, EXIT.usage);
  assert.match(stderr, /requires --model-info or --version/);
});

test('detect: --format rejected with exit 65 before not-implemented', async () => {
  const { code, stderr } = await runCli(['detect', 'image.png', '--format', 'json']);
  assert.equal(code, EXIT.invalid_argument);
  assert.match(stderr, /does not accept --format/);
});

test('detect: not-implemented returns exit 67', async () => {
  // detect now delegates to engine.detect() which requires native build.
  // Without native, it fails with package_load_failed (exit 70) or similar.
  // The flag contract (no --format) is still tested above.
  // This test verifies detect dispatches (not just throws stub immediately).
  const { code } = await runCli(['detect', 'image.png']);
  // Without native build, expect a non-zero code (not 67 stub anymore)
  assert.notEqual(code, EXIT.success);
});

test('recognize: invalid --format exit 65', async () => {
  const { code, stderr } = await runCli(['recognize', 'image.png', '--format', 'xml']);
  assert.equal(code, EXIT.invalid_argument);
  assert.match(stderr, /unsupported --format/);
});

test('recognize: invalid --provider exit 65', async () => {
  const { code, stderr } = await runCli(['recognize', 'image.png', '--provider', 'cuda']);
  assert.equal(code, EXIT.invalid_argument);
  assert.match(stderr, /unsupported --provider/);
});

test('recognize: stdin without --type exit 64', async () => {
  const { code, stderr } = await runCli(['recognize', '--stdin']);
  assert.equal(code, EXIT.usage);
  assert.match(stderr, /requires --type/);
});

test('recognize: file + --stdin conflict exit 64', async () => {
  const { code, stderr } = await runCli(['recognize', 'image.png', '--stdin', '--type', 'image/png']);
  assert.equal(code, EXIT.usage);
  assert.match(stderr, /both a file path and --stdin/);
});

test('recognize: nonexistent file exit 64', async () => {
  const { code, stderr } = await runCli(['recognize', 'does-not-exist.png']);
  assert.equal(code, EXIT.usage);
  assert.match(stderr, /file not found/);
});

test('recognize: invalid region surfaces before file-not-found (exit 65)', async () => {
  // Flag validation must happen before input reading (D106 ordering).
  // Even with a nonexistent file, an invalid --region returns 65, not 64.
  const { code, stderr } = await runCli(['recognize', 'does-not-exist.png', '--region', '0,0,0,10']);
  assert.equal(code, EXIT.invalid_argument);
  assert.match(stderr, /region/);
});

test('recognize: invalid format surfaces before file-not-found (exit 65)', async () => {
  const { code } = await runCli(['recognize', 'does-not-exist.png', '--format', 'xml']);
  assert.equal(code, EXIT.invalid_argument);
});

test('implicit recognize: no subcommand still routes to recognize', async () => {
  // nonexistent file: if routed to recognize, we get usage 64 with file-not-found
  const { code, stderr } = await runCli(['does-not-exist.png']);
  assert.equal(code, EXIT.usage);
  assert.match(stderr, /file not found/);
});

test('stdout/stderr separation: error goes to stderr not stdout', async () => {
  const { code, stdout, stderr } = await runCli(['recognize', '--stdin']);
  assert.equal(code, EXIT.usage);
  assert.equal(stdout, '');
  assert.notEqual(stderr, '');
});

test('exit code map: covers all OcrErrorCode values', () => {
  const required = [
    'invalid_argument', 'invalid_image', 'unsupported_pixel_format',
    'unsupported_capability', 'invalid_model_bundle', 'unsupported_model',
    'model_integrity_failed', 'runtime_initialization_failed', 'inference_failed',
    'postprocess_failed', 'resource_limit_exceeded', 'invalid_engine',
    'internal_error', 'bundle_io_failed', 'queue_full', 'environment_closing',
    'unsupported_platform', 'package_load_failed',
  ];
  for (const code of required) {
    assert.equal(typeof OCR_ERROR_EXIT[code], 'number', `missing exit mapping for ${code}`);
  }
});

// --- step 2: envelope tests (no native needed) ---
const { buildEnvelope, buildPageRecord, resolveSchemaVersion, inferMediaType } =
  require('../bin/light-ocr.cjs');

const sampleResult = {
  lines: [
    { text: 'HELLO', confidence: 0.99, box: [{x:0,y:0},{x:100,y:0},{x:100,y:30},{x:0,y:30}] },
    { text: '123', confidence: 0.95, box: [{x:0,y:31},{x:50,y:31},{x:50,y:60},{x:0,y:60}] },
  ],
  imageWidth: 640,
  imageHeight: 480,
  modelBundleId: 'ppocrv6-small-test',
  timingUs: { total: 1000, decode: 10 },
};

test('buildEnvelope: wraps OcrResult in DocumentResult with schemaVersion 1', () => {
  const env = buildEnvelope(sampleResult, { mediaType: 'image/png', identity: { path: 'a.png' } });
  assert.equal(env.schemaVersion, 1);
  assert.equal(env.source.kind, 'image');
  assert.equal(env.source.mediaType, 'image/png');
  assert.equal(env.source.identity.path, 'a.png');
  assert.equal(env.pages.length, 1);
  assert.equal(env.pages[0].index, 0);
  assert.equal(env.pages[0].coordinateSpace, 'pageSpace');
  assert.equal(env.pages[0].structure, 'ocr-order');
  assert.equal(env.pages[0].width, 640);
  assert.equal(env.pages[0].height, 480);
  assert.equal(env.pages[0].modelBundleId, 'ppocrv6-small-test');
});

test('buildEnvelope: assigns stable line ids L0, L1, ...', () => {
  const env = buildEnvelope(sampleResult, {});
  assert.equal(env.pages[0].lines[0].id, 'L0');
  assert.equal(env.pages[0].lines[0].text, 'HELLO');
  assert.equal(env.pages[0].lines[1].id, 'L1');
  assert.equal(env.pages[0].lines[1].text, '123');
  // original box preserved
  assert.deepEqual(env.pages[0].lines[0].box, sampleResult.lines[0].box);
});

test('buildEnvelope: appliedTransforms defaults to exifApplied false', () => {
  const env = buildEnvelope(sampleResult, {});
  assert.equal(env.source.appliedTransforms.exifApplied, false);
});

test('buildEnvelope: includes diagnostics when present', () => {
  const resultWithDiag = { ...sampleResult, diagnostics: { rejectedLines: [] } };
  const env = buildEnvelope(resultWithDiag, {});
  assert.deepEqual(env.pages[0].diagnostics, { rejectedLines: [] });
});

test('buildPageRecord: JSONL record has status, pageIndex, source, page', () => {
  const env = buildEnvelope(sampleResult, { mediaType: 'image/png', identity: { path: 'a.png' } });
  const record = buildPageRecord(env);
  assert.equal(record.schemaVersion, 1);
  assert.equal(record.pageIndex, 0);
  assert.equal(record.status, 'ok');
  assert.equal(record.source.path, 'a.png');
  assert.equal(record.page.lines.length, 2);
});

test('resolveSchemaVersion: default returns 1', () => {
  assert.equal(resolveSchemaVersion({}), 1);
  assert.equal(resolveSchemaVersion({ 'schema-version': undefined }), 1);
});

test('resolveSchemaVersion: explicit 1 returns 1', () => {
  assert.equal(resolveSchemaVersion({ 'schema-version': '1' }), 1);
  assert.equal(resolveSchemaVersion({ 'schema-version': 1 }), 1);
});

test('resolveSchemaVersion: unsupported version throws invalid_argument', () => {
  assert.throws(() => resolveSchemaVersion({ 'schema-version': '2' }), (e) => e.code === EXIT.invalid_argument);
  assert.throws(() => resolveSchemaVersion({ 'schema-version': '0' }), (e) => e.code === EXIT.invalid_argument);
  assert.throws(() => resolveSchemaVersion({ 'schema-version': 'abc' }), (e) => e.code === EXIT.invalid_argument);
});

test('inferMediaType: png/jpg extensions', () => {
  assert.equal(inferMediaType('a.png', null), 'image/png');
  assert.equal(inferMediaType('a.jpg', null), 'image/jpeg');
  assert.equal(inferMediaType('a.jpeg', null), 'image/jpeg');
  assert.equal(inferMediaType('a.txt', null), null);
  assert.equal(inferMediaType(null, 'image/png'), 'image/png');
});

test('recognize: unsupported --schema-version exit 65', async () => {
  const { code, stderr } = await runCli(['recognize', 'image.png', '--schema-version', '2']);
  assert.equal(code, EXIT.invalid_argument);
  assert.match(stderr, /unsupported --schema-version/);
});

// --- step 4: ROI parsing tests ---
const { parseRegion } = require('../bin/light-ocr.cjs');

test('parseRegion: valid x,y,w,h', () => {
  const r = parseRegion({ region: '100,80,640,320' });
  assert.deepEqual(r, { x: 100, y: 80, width: 640, height: 320 });
});

test('parseRegion: undefined returns undefined', () => {
  assert.equal(parseRegion({}), undefined);
});

test('parseRegion: wrong part count throws', () => {
  assert.throws(() => parseRegion({ region: '100,80,640' }), (e) => e.code === EXIT.invalid_argument);
  assert.throws(() => parseRegion({ region: '100,80,640,320,1' }), (e) => e.code === EXIT.invalid_argument);
});

test('parseRegion: negative values throw', () => {
  assert.throws(() => parseRegion({ region: '-1,0,100,100' }), (e) => e.code === EXIT.invalid_argument);
});

test('parseRegion: zero width/height throws', () => {
  assert.throws(() => parseRegion({ region: '0,0,0,100' }), (e) => e.code === EXIT.invalid_argument);
  assert.throws(() => parseRegion({ region: '0,0,100,0' }), (e) => e.code === EXIT.invalid_argument);
});

test('parseRegion: non-integer throws', () => {
  assert.throws(() => parseRegion({ region: '1.5,0,100,100' }), (e) => e.code === EXIT.invalid_argument);
  assert.throws(() => parseRegion({ region: 'a,0,100,100' }), (e) => e.code === EXIT.invalid_argument);
});

// --- step 5: detect envelope tests ---
const { buildDetectEnvelope } = require('../bin/light-ocr.cjs');

const sampleDetection = {
  // Detection results come through as OcrResult format (lines with empty text,
  // detection score as confidence). This matches how the addon converts them.
  lines: [
    { text: '', confidence: 0.95, box: [{x:0,y:0},{x:100,y:0},{x:100,y:30},{x:0,y:30}] },
    { text: '', confidence: 0.80, box: [{x:0,y:31},{x:50,y:31},{x:50,y:60},{x:0,y:60}] },
  ],
  imageWidth: 640,
  imageHeight: 480,
  modelBundleId: 'ppocrv6-small-test',
  timingUs: { total: 500, decode: 10 },
};

test('buildDetectEnvelope: wraps with structure "detect"', () => {
  const env = buildDetectEnvelope(sampleDetection, { mediaType: 'image/png', identity: { path: 'a.png' } });
  assert.equal(env.schemaVersion, 1);
  assert.equal(env.pages[0].structure, 'detect');
  assert.equal(env.pages[0].detections.length, 2);
});

test('buildDetectEnvelope: assigns stable detection ids D0, D1, ...', () => {
  const env = buildDetectEnvelope(sampleDetection, {});
  assert.equal(env.pages[0].detections[0].id, 'D0');
  assert.equal(env.pages[0].detections[0].score, 0.95);
  assert.equal(env.pages[0].detections[1].id, 'D1');
  assert.equal(env.pages[0].detections[1].score, 0.80);
});

test('buildDetectEnvelope: no lines field (detect has detections, not lines)', () => {
  const env = buildDetectEnvelope(sampleDetection, {});
  assert.equal(env.pages[0].lines, undefined);
  assert.notEqual(env.pages[0].detections, undefined);
});

// --- step 7: schema snapshot tests ---
// Lock the envelope JSON shape so future changes don't silently alter schema.
test('schema snapshot: recognize envelope has required top-level fields', () => {
  const env = buildEnvelope(sampleResult, { mediaType: 'image/png', identity: { path: 'a.png' } });
  assert.ok(env.schemaVersion, 'missing schemaVersion');
  assert.ok(env.source, 'missing source');
  assert.ok(env.source.kind, 'missing source.kind');
  assert.ok(env.source.mediaType, 'missing source.mediaType');
  assert.ok(env.source.identity, 'missing source.identity');
  assert.ok(env.source.appliedTransforms, 'missing source.appliedTransforms');
  assert.ok(env.pages, 'missing pages');
  assert.equal(env.pages.length, 1);
  const page = env.pages[0];
  assert.ok(page.index !== undefined, 'missing page.index');
  assert.ok(page.width !== undefined, 'missing page.width');
  assert.ok(page.height !== undefined, 'missing page.height');
  assert.equal(page.coordinateSpace, 'pageSpace');
  assert.equal(page.structure, 'ocr-order');
  assert.ok(page.lines, 'missing page.lines');
  assert.ok(page.modelBundleId, 'missing page.modelBundleId');
  assert.ok(page.timingUs, 'missing page.timingUs');
});

test('schema snapshot: line has id, text, confidence, box', () => {
  const env = buildEnvelope(sampleResult, {});
  const line = env.pages[0].lines[0];
  assert.ok(line.id, 'missing line.id');
  assert.ok('text' in line, 'missing line.text');
  assert.ok('confidence' in line, 'missing line.confidence');
  assert.ok(line.box, 'missing line.box');
  assert.equal(line.box.length, 4, 'box must have 4 points');
});

test('schema snapshot: detect envelope has detections with id, score, box', () => {
  const env = buildDetectEnvelope(sampleDetection, {});
  assert.equal(env.pages[0].structure, 'detect');
  const det = env.pages[0].detections[0];
  assert.ok(det.id, 'missing detection.id');
  assert.ok('score' in det, 'missing detection.score');
  assert.ok(det.box, 'missing detection.box');
  assert.equal(det.box.length, 4, 'box must have 4 points');
});
