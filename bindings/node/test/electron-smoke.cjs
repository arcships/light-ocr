'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { app } = require('electron');

const repositoryRoot = path.resolve(__dirname, '../../..');
const bundlePath = path.resolve(
  process.env.LIGHT_OCR_MODEL_BUNDLE ||
    path.join(repositoryRoot, 'models/generated/ppocrv6-small-onnx-20260714.2'),
);

function loadHelloFixture() {
  const directory = path.join(repositoryRoot, 'corpus/fixtures/generated-hello-123');
  const metadata = JSON.parse(fs.readFileSync(path.join(directory, 'fixture.json'), 'utf8'));
  return {
    data: fs.readFileSync(path.join(directory, 'pixels.bin')),
    width: metadata.width,
    height: metadata.height,
    stride: metadata.stride,
    pixelFormat: metadata.pixelFormat,
  };
}

async function run() {
  const { createEngine } = require('../js/index.cjs');
  const engine = await createEngine({
    bundlePath,
    execution: { provider: 'cpu' },
    detection: { strategy: 'bounded', maxSide: 960 },
    intraOpThreads: 2,
    interOpThreads: 1,
    recognitionBatchSize: 1,
    queueCapacity: 1,
  });
  try {
    const result = await engine.recognize(loadHelloFixture());
    assert.deepEqual(result.lines.map((line) => line.text), ['HELLO 123']);
    process.stdout.write(`${JSON.stringify({
      electron: process.versions.electron,
      node: process.versions.node,
      platform: `${process.platform}-${process.arch}`,
      requestedProvider: 'cpu',
      actualProvider: engine.info.executionProvider,
      text: result.lines.map((line) => line.text).join('\n'),
    })}\n`);
  } finally {
    await engine.close();
  }
}

app.disableHardwareAcceleration();
app.whenReady()
  .then(run)
  .then(() => app.exit(0))
  .catch((error) => {
    console.error(error);
    app.exit(1);
  });
