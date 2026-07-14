'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

async function main() {
  const fixtureDirectory = process.env.LIGHT_OCR_SMOKE_FIXTURE;
  assert.ok(fixtureDirectory, 'LIGHT_OCR_SMOKE_FIXTURE is required');
  const metadata = JSON.parse(
    fs.readFileSync(path.join(fixtureDirectory, 'fixture.json'), 'utf8'),
  );
  const pixels = fs.readFileSync(path.join(fixtureDirectory, 'pixels.bin'));

  const cjs = require('@arcships/light-ocr');
  const esm = await import('@arcships/light-ocr');
  assert.strictEqual(esm.createEngine, cjs.createEngine);
  assert.strictEqual(esm.OcrError, cjs.OcrError);

  const engine = await cjs.createEngine();
  try {
    assert.equal(engine.info.modelBundleId, 'ppocrv6-small-onnx-20260714.1');
    assert.equal(engine.info.detectionStrategy, 'bounded');
    assert.equal(engine.info.detectionMaxSide, 960);
    assert.equal(engine.info.defaultRecognitionBatchSize, 1);
    const result = await engine.recognize({
      data: pixels,
      width: metadata.width,
      height: metadata.height,
      stride: metadata.stride,
      pixelFormat: metadata.pixelFormat,
    });
    assert.deepEqual(result.lines.map((line) => line.text), ['HELLO 123']);
  } finally {
    await engine.close();
  }
  process.stdout.write(
    `${JSON.stringify({ ok: true, node: process.version, platform: process.platform, arch: process.arch })}\n`,
  );
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
