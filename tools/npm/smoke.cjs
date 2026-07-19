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
    assert.equal(engine.info.modelBundleId, 'ppocrv6-small-native-20260719.1');
    assert.equal(engine.info.detectionStrategy, 'bounded');
    assert.equal(engine.info.detectionMaxSide, 960);
    assert.equal(engine.info.defaultRecognitionBatchSize, 1);
    if (process.platform === 'darwin') {
      assert.deepEqual(
        engine.info.execution.selectionTrace.orderedCandidates,
        ['apple', 'cpu'],
      );
      assert.ok(engine.info.execution.providerCapabilities.some(
        (capability) => capability.provider === 'apple'
          && capability.packageIncluded,
      ));
    } else {
      assert.deepEqual(
        engine.info.execution.selectionTrace.orderedCandidates,
        ['webgpu', 'cpu'],
      );
      assert.ok(engine.info.execution.providerCapabilities.some(
        (capability) => capability.provider === 'webgpu'
          && capability.packageIncluded,
      ));
      assert.ok(['webgpu', 'cpu'].includes(
        engine.info.execution.selectionTrace.selectedProvider,
      ));
    }
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

  if (process.platform === 'darwin') {
    const apple = await cjs.createEngine({
      execution: {
        provider: 'apple',
        precision: 'fp16',
        sessionFallback: 'error',
      },
    });
    try {
      assert.equal(apple.info.execution.requestedProvider, 'apple');
      const detection = apple.info.execution.sessions.detection;
      assert.equal(apple.info.executionProvider, 'CoreML');
      assert.equal(detection.sessionFallback, false);
      assert.match(detection.qualificationId, /^apple-/);
    } finally {
      await apple.close();
    }
  }

  const tiledPixels = Buffer.alloc(2048 * 2048 * 3, 255);
  const offsetX = 600;
  const offsetY = 760;
  for (let row = 0; row < metadata.height; ++row) {
    pixels.copy(
      tiledPixels,
      ((offsetY + row) * 2048 + offsetX) * 3,
      row * metadata.stride,
      row * metadata.stride + metadata.width * 3,
    );
  }
  const tiled = await cjs.createEngine({ detection: { strategy: 'tiled' } });
  try {
    assert.equal(tiled.info.detectionStrategy, 'tiled');
    assert.equal(tiled.info.tiledDetection.contractVersion, 'tiled-v1');
    const result = await tiled.recognize({
      data: tiledPixels,
      width: 2048,
      height: 2048,
      stride: 2048 * 3,
      pixelFormat: 'bgr8',
    }, { includeDiagnostics: true });
    assert.deepEqual(result.lines.map((line) => line.text), ['HELLO 123']);
    assert.equal(result.diagnostics.detectionPasses.length, 4);
    assert.equal(result.diagnostics.maxLiveDetectionPassBuffers, 1);
    assert.ok(result.diagnostics.suppressedDuplicateBoxes >= 1);
  } finally {
    await tiled.close();
  }
  process.stdout.write(
    `${JSON.stringify({ ok: true, node: process.version, platform: process.platform, arch: process.arch })}\n`,
  );
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
