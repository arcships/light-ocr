'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const { pathToFileURL } = require('node:url');
const { Worker } = require('node:worker_threads');

const { createEngine, OcrError } = require('../js/index.cjs');

const repositoryRoot = path.resolve(__dirname, '../../..');
const bundlePath = path.resolve(
  process.env.LIGHT_OCR_MODEL_BUNDLE ||
    path.join(repositoryRoot, 'models/generated/ppocrv6-small-onnx-20260713.1'),
);

function loadFixture(id) {
  const directory = path.join(repositoryRoot, 'corpus/fixtures', id);
  const metadata = JSON.parse(fs.readFileSync(path.join(directory, 'fixture.json'), 'utf8'));
  return {
    data: fs.readFileSync(path.join(directory, 'pixels.bin')),
    width: metadata.width,
    height: metadata.height,
    stride: metadata.stride,
    pixelFormat: metadata.pixelFormat,
  };
}

function delay(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

test('ESM and CommonJS facades expose the same API', async () => {
  const esm = await import(pathToFileURL(path.join(repositoryRoot, 'bindings/node/js/index.mjs')));
  assert.strictEqual(esm.createEngine, createEngine);
  assert.strictEqual(esm.OcrError, OcrError);
});

test('loads PP-OCRv6, snapshots pixels, maps results, and closes idempotently', async () => {
  const engine = await createEngine({ bundlePath });
  assert.equal(engine.info.modelBundleId, 'ppocrv6-small-onnx-20260713.1');
  assert.equal(engine.info.adapter.scheduler, 'dedicated_fifo');
  assert.equal(engine.info.limits.maxConcurrentCalls, 1);
  assert.ok(Object.isFrozen(engine.info));
  assert.ok(Object.isFrozen(engine.info.adapter));

  const image = loadFixture('generated-hello-123');
  const storage = Buffer.alloc(image.data.length + 31);
  image.data.copy(storage, 17);
  image.data = storage.subarray(17, 17 + image.data.length);
  const recognition = engine.recognize(image, { includeDiagnostics: true });
  image.data.fill(0);
  const result = await recognition;
  assert.deepEqual(result.lines.map((line) => line.text), ['HELLO 123']);
  assert.equal(result.imageWidth, 800);
  assert.equal(result.imageHeight, 180);
  assert.equal(result.modelBundleId, engine.info.modelBundleId);
  assert.equal(result.diagnostics.acceptedBoxes, 1);

  const closeA = engine.close();
  const closeB = engine.close();
  assert.strictEqual(closeA, closeB);
  await closeA;
  await assert.rejects(
    engine.recognize(loadFixture('generated-blank')),
    (error) => error instanceof OcrError && error.code === 'invalid_engine',
  );
});

test('validates input and reports adapter errors as OcrError', async () => {
  await assert.rejects(
    createEngine({ bundlePath: path.join(repositoryRoot, 'models/does-not-exist') }),
    (error) => error instanceof OcrError && error.code === 'bundle_io_failed',
  );

  const engine = await createEngine({ bundlePath });
  const image = loadFixture('generated-blank');
  await assert.rejects(
    engine.recognize(image, { misspelledOption: true }),
    (error) => error instanceof OcrError && error.code === 'invalid_argument',
  );
  await assert.rejects(
    engine.recognize(image, []),
    (error) => error instanceof OcrError && error.code === 'invalid_argument',
  );
  await assert.rejects(
    engine.recognize({ ...image, data: image.data.subarray(0, 1) }),
    (error) => error instanceof OcrError && error.code === 'invalid_image',
  );
  await assert.rejects(
    engine.recognize({
      ...image,
      data: image.data.subarray(0, 1),
      width: 1,
      height: engine.info.limits.maxHeight,
      stride: Number.MAX_SAFE_INTEGER,
    }),
    (error) => error instanceof OcrError && error.code === 'invalid_image',
  );
  if (typeof SharedArrayBuffer === 'function') {
    const shared = new Uint8Array(new SharedArrayBuffer(image.data.length));
    await assert.rejects(
      engine.recognize({ ...image, data: shared }),
      (error) => error instanceof OcrError && error.code === 'invalid_image',
    );
  }
  await engine.close();
});

test('secure bundle loading rejects a symbolic-link root', async (t) => {
  if (process.platform === 'win32') {
    t.skip('Windows reparse-point coverage runs in the Windows release matrix');
    return;
  }
  const temporary = fs.mkdtempSync(path.join(process.env.TMPDIR || '/tmp', 'light-ocr-node-'));
  const linkedBundle = path.join(temporary, 'bundle');
  try {
    fs.symlinkSync(bundlePath, linkedBundle, 'dir');
    await assert.rejects(
      createEngine({ bundlePath: linkedBundle }),
      (error) => error instanceof OcrError && error.code === 'bundle_io_failed',
    );
  } finally {
    fs.rmSync(temporary, { recursive: true, force: true });
  }
});

test('enforces bounded admission and restores capacity after queued cancellation', async () => {
  const oneSlot = await createEngine({ bundlePath, queueCapacity: 1 });
  const slowImage = loadFixture('paddleocr-xfund-form');
  const first = oneSlot.recognize(slowImage);
  await assert.rejects(
    oneSlot.recognize(loadFixture('generated-blank')),
    (error) => error instanceof OcrError && error.code === 'queue_full',
  );
  await first;
  await oneSlot.close();

  const engine = await createEngine({ bundlePath, queueCapacity: 2 });
  const running = engine.recognize(slowImage);
  const controller = new AbortController();
  const reason = new Error('cancel queued request');
  const queued = engine.recognize(loadFixture('generated-hello-123'), {
    signal: controller.signal,
  });
  controller.abort(reason);
  await assert.rejects(queued, (error) => error === reason);

  const afterCancel = engine.recognize(loadFixture('generated-hello-123'));
  const [, recovered] = await Promise.all([running, afterCancel]);
  assert.deepEqual(recovered.lines.map((line) => line.text), ['HELLO 123']);
  await engine.close();
});

test('enforces pending snapshot bytes and restores the byte budget', async () => {
  const hello = loadFixture('generated-hello-123');
  const engine = await createEngine({
    bundlePath,
    queueCapacity: 2,
    maxPendingInputBytes: hello.data.length + 128,
  });
  const first = engine.recognize(hello);
  await assert.rejects(
    engine.recognize(loadFixture('generated-blank')),
    (error) => error instanceof OcrError && error.code === 'queue_full',
  );
  await first;
  const recovered = await engine.recognize(loadFixture('generated-blank'));
  assert.deepEqual(recovered.lines, []);
  await engine.close();
});

test('AbortSignal rejects before admission and discards a running result cooperatively', async (t) => {
  const engine = await createEngine({ bundlePath, queueCapacity: 1 });
  const preAborted = new AbortController();
  const preReason = { stage: 'before-admission' };
  preAborted.abort(preReason);
  await assert.rejects(
    engine.recognize(loadFixture('generated-blank'), { signal: preAborted.signal }),
    (error) => error === preReason,
  );

  const controller = new AbortController();
  const reason = new Error('discard running result');
  let eventLoopTicks = 0;
  const heartbeat = setInterval(() => {
    ++eventLoopTicks;
  }, 5);
  t.after(() => clearInterval(heartbeat));
  const recognition = engine.recognize(loadFixture('paddleocr-xfund-form'), {
    signal: controller.signal,
  });
  await delay(25);
  controller.abort(reason);
  await assert.rejects(recognition, (error) => error === reason);
  await assert.rejects(
    engine.recognize(loadFixture('generated-blank')),
    (error) => error instanceof OcrError && error.code === 'queue_full',
  );
  await engine.close();
  clearInterval(heartbeat);
  assert.ok(eventLoopTicks > 0, 'inference and close must not block the JavaScript event loop');
});

test('close drains requests admitted before it', async () => {
  const engine = await createEngine({ bundlePath });
  const recognition = engine.recognize(loadFixture('generated-hello-123'));
  const closing = engine.close();
  const result = await recognition;
  assert.equal(result.lines[0].text, 'HELLO 123');
  await closing;
});

test('worker environment teardown closes an unclosed engine', async () => {
  const modulePath = path.join(repositoryRoot, 'bindings/node/js/index.cjs');
  const source = `
    const { parentPort, workerData } = require('node:worker_threads');
    const { createEngine } = require(workerData.modulePath);
    createEngine({ bundlePath: workerData.bundlePath }).then(() => parentPort.postMessage('ready'));
  `;
  const worker = new Worker(source, {
    eval: true,
    workerData: { modulePath, bundlePath },
  });
  await new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error('worker did not initialize')), 10_000);
    worker.once('message', (message) => {
      clearTimeout(timeout);
      assert.equal(message, 'ready');
      resolve();
    });
    worker.once('error', reject);
  });
  const exitCode = await new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error('worker teardown did not complete')), 10_000);
    worker.once('exit', (code) => {
      clearTimeout(timeout);
      resolve(code);
    });
    worker.once('error', reject);
  });
  assert.equal(exitCode, 0);
});
