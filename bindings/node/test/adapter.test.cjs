'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const { pathToFileURL } = require('node:url');
const { Worker } = require('node:worker_threads');
const zlib = require('node:zlib');

const { createEngine, OcrError } = require('../js/index.cjs');
const { validateRuntimeDescriptor } = require('../js/load-native.cjs');

const repositoryRoot = path.resolve(__dirname, '../../..');
const bundlePath = path.resolve(
  process.env.LIGHT_OCR_MODEL_BUNDLE ||
    path.join(repositoryRoot, 'models/generated/ppocrv6-small-onnx-20260714.2'),
);
const appleBundlePath = process.env.LIGHT_OCR_APPLE_MODEL_BUNDLE
  ? path.resolve(process.env.LIGHT_OCR_APPLE_MODEL_BUNDLE)
  : undefined;
const runtimePolicy = process.env.LIGHT_OCR_RUNTIME_DESCRIPTOR
  ? validateRuntimeDescriptor(path.resolve(process.env.LIGHT_OCR_RUNTIME_DESCRIPTOR)).runtimePolicy
  : undefined;

const encodedBlankPng = Buffer.from(
  'iVBORw0KGgoAAAANSUhEUgAAAAIAAAADCAIAAAA2iEnWAAAAFUlEQVR4nGP8//8/AwMDEwMDA4ICADkbAwP+wj6MAAAAAElFTkSuQmCC',
  'base64',
);
const encodedBlankJpeg = Buffer.from(
  '/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAgGBgcGBQgHBwcJCQgKDBQNDAsLDBkSEw8UHRofHh0aHBwgJC4nICIsIxwcKDcpLDAxNDQ0Hyc5PTgyPC4zNDL/2wBDAQkJCQwLDBgNDRgyIRwhMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjL/wAARCAADAAIDASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwD3+iiigD//2Q==',
  'base64',
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

function crc32(data) {
  let crc = 0xffffffff;
  for (const byte of data) {
    crc ^= byte;
    for (let bit = 0; bit < 8; ++bit) {
      crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);
    }
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function pngChunk(type, data) {
  const typeBytes = Buffer.from(type, 'ascii');
  const chunk = Buffer.alloc(12 + data.length);
  chunk.writeUInt32BE(data.length, 0);
  typeBytes.copy(chunk, 4);
  data.copy(chunk, 8);
  chunk.writeUInt32BE(crc32(Buffer.concat([typeBytes, data])), 8 + data.length);
  return chunk;
}

function encodeBgrFixtureAsPng(image) {
  assert.equal(image.pixelFormat, 'bgr8');
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(image.width, 0);
  ihdr.writeUInt32BE(image.height, 4);
  ihdr.set([8, 2, 0, 0, 0], 8);

  const scanlines = Buffer.alloc(image.height * (1 + image.width * 3));
  for (let y = 0; y < image.height; ++y) {
    const row = y * (1 + image.width * 3);
    scanlines[row] = 0;
    for (let x = 0; x < image.width; ++x) {
      const source = y * image.stride + x * 3;
      const destination = row + 1 + x * 3;
      scanlines[destination] = image.data[source + 2];
      scanlines[destination + 1] = image.data[source + 1];
      scanlines[destination + 2] = image.data[source];
    }
  }

  return Buffer.concat([
    Buffer.from('89504e470d0a1a0a', 'hex'),
    pngChunk('IHDR', ihdr),
    pngChunk('IDAT', zlib.deflateSync(scanlines)),
    pngChunk('IEND', Buffer.alloc(0)),
  ]);
}

function delay(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}


test('validates runtime descriptor artifacts before native loading', () => {
  const os = require('node:os');
  const crypto = require('node:crypto');
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'light-ocr-descriptor-'));
  try {
    const native = path.join(directory, 'native');
    fs.mkdirSync(native);
    const addon = path.join(native, 'light_ocr_node.node');
    const runtimeName = process.platform === 'win32'
      ? 'onnxruntime.dll'
      : process.platform === 'darwin'
        ? 'libonnxruntime.1.22.0.dylib'
        : 'libonnxruntime.so.1';
    const runtime = path.join(native, runtimeName);
    fs.writeFileSync(addon, 'addon');
    fs.writeFileSync(runtime, 'runtime');
    const record = (filename) => ({
      path: path.relative(directory, filename).replaceAll(path.sep, '/'),
      bytes: fs.statSync(filename).size,
      sha256: crypto.createHash('sha256').update(fs.readFileSync(filename)).digest('hex'),
    });
    const machine = process.arch === 'arm64' ? 'arm64' : 'x86_64';
    const platformId = process.platform === 'darwin'
      ? `macos-${process.arch}`
      : process.platform === 'win32'
        ? 'windows-x64'
        : 'linux-x64';
    const descriptor = {
      schemaVersion: '2.0',
      platform: {
        id: platformId,
        os: process.platform,
        architecture: machine,
        ...(process.platform === 'linux' ? { libc: 'glibc' } : {}),
      },
      runtime: {
        flavor: 'cpu',
        kind: 'onnxruntime-cpu',
        version: '1.22.0',
        abi: 'onnxruntime-c-api-22',
        artifacts: [record(runtime)],
      },
      qualificationOnly: false,
      released: true,
      autoPolicy: {
        id: `${platformId}-v1`,
        version: 1,
        providers: process.platform === 'darwin' ? ['apple', 'cpu'] : ['cpu'],
      },
      providers: {
        cpu: {
          runtimeProvider: 'CPUExecutionProvider',
          qualificationId: 'cpu-baseline-v1',
          artifacts: [record(runtime)],
        },
        ...(process.platform === 'darwin'
          ? {
              apple: {
                runtimeProvider: 'CoreML',
                qualificationId: 'apple-open-macos-v1',
                artifacts: [record(addon)],
              },
            }
          : {}),
      },
      addon: record(addon),
    };
    const descriptorPath = path.join(native, 'runtime-descriptor.json');
    fs.writeFileSync(descriptorPath, `${JSON.stringify(descriptor)}\n`);
    assert.equal(validateRuntimeDescriptor(descriptorPath).addon, addon);
    const nested = path.join(native, 'providers', 'unexpected.bin');
    fs.mkdirSync(path.dirname(nested), { recursive: true });
    fs.writeFileSync(nested, 'untracked');
    assert.throws(
      () => validateRuntimeDescriptor(descriptorPath),
      (error) => error.name === 'OcrError' && error.code === 'package_load_failed',
    );
    fs.rmSync(path.dirname(nested), { recursive: true });
    fs.writeFileSync(runtime, 'mutated');
    assert.throws(
      () => validateRuntimeDescriptor(descriptorPath),
      (error) => error.name === 'OcrError' && error.code === 'package_load_failed',
    );
    descriptor.providers.cpu.artifacts[0].path = '../outside';
    fs.writeFileSync(descriptorPath, `${JSON.stringify(descriptor)}\n`);
    assert.throws(
      () => validateRuntimeDescriptor(descriptorPath),
      (error) => error.name === 'OcrError' && error.code === 'package_load_failed',
    );
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
});

test(
  'validates the self-contained WebGPU plugin runtime descriptor',
  { skip: process.platform !== 'linux' || process.arch !== 'x64' },
  () => {
    const os = require('node:os');
    const crypto = require('node:crypto');
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'light-ocr-webgpu-descriptor-'));
    try {
      const native = path.join(directory, 'native');
      fs.mkdirSync(native);
      const addon = path.join(native, 'light_ocr_node.node');
      const runtime = path.join(native, 'libonnxruntime.so.1');
      const provider = path.join(native, 'libonnxruntime_providers_webgpu.so');
      fs.writeFileSync(addon, 'addon');
      fs.writeFileSync(runtime, 'webgpu-runtime');
      fs.writeFileSync(provider, 'webgpu-provider');
      const record = (filename) => ({
        path: path.relative(directory, filename).replaceAll(path.sep, '/'),
        bytes: fs.statSync(filename).size,
        sha256: crypto.createHash('sha256').update(fs.readFileSync(filename)).digest('hex'),
      });
      const runtimeRecord = record(runtime);
      const providerRecord = record(provider);
      const descriptor = {
        schemaVersion: '2.0',
        platform: {
          id: 'linux-x64',
          os: 'linux',
          architecture: 'x86_64',
          libc: 'glibc',
        },
        runtime: {
          flavor: 'webgpu',
          kind: 'onnxruntime-plugin-webgpu',
          version: '1.24.4',
          abi: 'onnxruntime-c-api-24-plugin-ep-0.1',
          artifacts: [runtimeRecord, providerRecord],
        },
        qualificationOnly: true,
        released: false,
        autoPolicy: { id: 'linux-x64-v1', version: 1, providers: ['webgpu', 'cpu'] },
        providers: {
          webgpu: {
            runtimeProvider: 'WebGpuExecutionProvider',
            providerVersion: '0.1.0',
            qualificationId: 'webgpu-poc-v1',
            providerLibrary: providerRecord,
            artifacts: [providerRecord],
          },
          cpu: {
            runtimeProvider: 'CPUExecutionProvider',
            qualificationId: 'cpu-baseline-v1',
            artifacts: [runtimeRecord],
          },
        },
        addon: record(addon),
      };
      const descriptorPath = path.join(native, 'runtime-descriptor.json');
      fs.writeFileSync(descriptorPath, `${JSON.stringify(descriptor)}\n`);
      assert.equal(
        validateRuntimeDescriptor(descriptorPath).runtimePolicy.runtimeFlavor,
        'webgpu',
      );

      descriptor.providers.webgpu.providerLibrary.sha256 = '0'.repeat(64);
      fs.writeFileSync(descriptorPath, `${JSON.stringify(descriptor)}\n`);
      assert.throws(
        () => validateRuntimeDescriptor(descriptorPath),
        (error) =>
          error.name === 'OcrError' &&
          error.code === 'package_load_failed' &&
          /hash mismatch|library contract/.test(error.message),
      );
    } finally {
      fs.rmSync(directory, { recursive: true, force: true });
    }
  },
);

test('native addon rejects a forged runtime ABI before candidate creation', () => {
  const binary = process.env.LIGHT_OCR_NODE_BINARY;
  const descriptorPath = process.env.LIGHT_OCR_RUNTIME_DESCRIPTOR;
  assert.ok(binary);
  assert.ok(descriptorPath);
  const binding = require(path.resolve(binary));
  const policy = validateRuntimeDescriptor(path.resolve(descriptorPath)).runtimePolicy;
  assert.ok(Object.isFrozen(binding.runtimeContract));
  assert.ok(Object.isFrozen(binding.runtimeContract.orderedCandidates));
  assert.deepEqual(binding.runtimeContract.orderedCandidates, policy.orderedCandidates);
  assert.throws(
    () => binding.createEngine({ bundlePath }, { ...policy, runtimeAbi: 'forged-abi' }),
    (error) =>
      error.name === 'OcrError' &&
      error.code === 'package_load_failed' &&
      error.creationTrace === undefined,
  );
});

test('ESM and CommonJS facades expose the same API', async () => {
  const esm = await import(pathToFileURL(path.join(repositoryRoot, 'bindings/node/js/index.mjs')));
  assert.strictEqual(esm.createEngine, createEngine);
  assert.strictEqual(esm.OcrError, OcrError);
});

test('loads PP-OCRv6, snapshots pixels, maps results, and closes idempotently', async () => {
  const engine = await createEngine({ bundlePath });
  assert.equal(engine.info.modelBundleId, 'ppocrv6-small-onnx-20260714.2');
  assert.equal(engine.info.normalizedConfigSchemaVersion, '1.2');
  assert.equal(engine.info.detectionStrategy, 'bounded');
  assert.equal(engine.info.detectionMaxSide, 960);
  assert.equal(engine.info.defaultRecognitionBatchSize, 1);
  assert.equal(engine.info.adapter.scheduler, 'dedicated_fifo');
  assert.equal(engine.info.limits.maxConcurrentCalls, 1);
  assert.equal(engine.info.limits.maxDetectionTiles, 100);
  assert.equal(engine.info.capabilities.tiledDetection, true);
  assert.equal(engine.info.tiledDetection, undefined);
  assert.equal(engine.info.executionProvider, 'CPUExecutionProvider');
  assert.equal(engine.info.execution.requestedProvider, 'auto');
  assert.ok(runtimePolicy);
  const expectedAutoAttempts = runtimePolicy.orderedCandidates[0] === 'apple'
    ? [
        {
          provider: 'apple',
          status: 'skipped',
          creationReason: 'model_compute_unsupported',
        },
        { provider: 'cpu', status: 'selected' },
      ]
    : [{ provider: 'cpu', status: 'selected' }];
  assert.deepEqual(engine.info.execution.selectionTrace, {
    requestedProvider: 'auto',
    policyId: runtimePolicy.id,
    policyVersion: runtimePolicy.version,
    orderedCandidates: runtimePolicy.orderedCandidates,
    attempts: expectedAutoAttempts,
    selectedProvider: 'cpu',
  });
  assert.equal(engine.info.execution.sessionFallback, 'error');
  assert.equal(engine.info.execution.cpuPartition, 'allow');
  assert.equal(engine.info.execution.performanceHint, 'latency');
  assert.equal(engine.info.execution.requestedPrecision, 'auto');
  assert.deepEqual(engine.info.execution.providerCapabilities, [{
    provider: 'cpu',
    packageIncluded: true,
    deviceAvailable: true,
    deviceValidated: true,
  }]);
  assert.deepEqual(
    engine.info.execution.sessions.detection.actualProviderChain,
    ['CPUExecutionProvider'],
  );
  assert.deepEqual(
    engine.info.execution.sessions.recognition.actualProviderChain,
    ['CPUExecutionProvider'],
  );
  assert.equal(
    engine.info.execution.sessions.detection.modelId,
    'PP-OCRv6_small_det_onnx',
  );
  assert.equal(
    engine.info.execution.sessions.recognition.modelId,
    'PP-OCRv6_small_rec_onnx',
  );
  assert.match(engine.info.execution.sessions.detection.modelSha256, /^[a-f0-9]{64}$/);
  assert.equal(engine.info.execution.sessions.detection.precision, 'fp32');
  assert.equal(engine.info.execution.sessions.detection.shapePolicy, 'dynamic');
  assert.equal(engine.info.execution.sessions.detection.deviceValidated, true);
  assert.equal(engine.info.execution.sessions.detection.sessionFallback, false);
  assert.equal(engine.info.execution.sessions.detection.fallbackReason, undefined);
  assert.ok(Object.isFrozen(engine.info));
  assert.ok(Object.isFrozen(engine.info.adapter));
  assert.ok(Object.isFrozen(engine.info.execution.sessions.detection));

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
  assert.equal(result.diagnostics.detectionInputWidth, 800);
  assert.equal(result.diagnostics.detectionInputHeight, 192);
  assert.equal(result.diagnostics.rawDetectionBoxes, 1);
  assert.equal(result.diagnostics.suppressedDuplicateBoxes, 0);
  assert.equal(result.diagnostics.maxLiveDetectionPassBuffers, 1);
  assert.equal(result.diagnostics.detectionPasses.length, 1);
  assert.equal(result.timingUs.detectionMerge, 0);
  assert.deepEqual(result.diagnostics.recognitionBatchShapes.map((shape) => shape.batchSize), [1]);
  assert.equal(result.timingUs.decode, 0);

  const closeA = engine.close();
  const closeB = engine.close();
  assert.strictEqual(closeA, closeB);
  await closeA;
  await assert.rejects(
    engine.recognize(loadFixture('generated-blank')),
    (error) => error instanceof OcrError && error.code === 'invalid_engine',
  );
});

test('exposes open Apple routing and validation status', {
  skip: appleBundlePath === undefined,
}, async () => {
  const image = loadFixture('generated-hello-123');
  const interactive = await createEngine({
    bundlePath: appleBundlePath,
    execution: { provider: 'apple', precision: 'fp16' },
  });
  try {
    assert.equal(interactive.info.executionProvider, 'CoreML');
    assert.equal(interactive.info.execution.requestedProvider, 'apple');
    assert.deepEqual(
      interactive.info.execution.sessions.detection.actualProviderChain,
      ['CoreML(MLNeuralEngine,MLCPU)'],
    );
    assert.deepEqual(
      interactive.info.execution.sessions.recognition.actualProviderChain,
      ['CoreML(MLNeuralEngine,MLCPU)', 'CoreML(MLGPU)'],
    );
    assert.match(
      interactive.info.execution.sessions.detection.qualificationId,
      /^apple-/,
    );
    assert.match(
      interactive.info.execution.sessions.detection.deviceFamily,
      /^Apple M/,
    );
    assert.equal(
      interactive.info.execution.sessions.detection.deviceValidated,
      interactive.info.execution.sessions.detection.deviceFamily.startsWith('Apple M4'),
    );
    assert.ok(
      interactive.info.execution.sessions.detection.operatingSystem.length > 0,
    );
    const result = await interactive.recognize(image, { includeDiagnostics: true });
    assert.deepEqual(result.lines.map((line) => line.text), ['HELLO 123']);
    assert.deepEqual(
      result.diagnostics.recognitionBatchShapes.map((shape) => shape.computeUnit),
      ['ane'],
    );
    assert.match(result.diagnostics.recognitionBatchShapes[0].modelId, /_coreml_fp16_/);
    assert.match(result.diagnostics.recognitionBatchShapes[0].shapeBucket, /^w\d{4}$/);
  } finally {
    await interactive.close();
  }

  const strict = await createEngine({
    bundlePath: appleBundlePath,
    execution: {
      provider: 'apple',
      precision: 'fp16',
      cpuPartition: 'forbid',
    },
  });
  try {
    assert.deepEqual(
      strict.info.execution.sessions.detection.actualProviderChain,
      ['CoreML(MLGPU)'],
    );
    assert.deepEqual(
      strict.info.execution.sessions.recognition.actualProviderChain,
      ['CoreML(MLGPU)'],
    );
    const result = await strict.recognize(image, { includeDiagnostics: true });
    assert.deepEqual(result.lines.map((line) => line.text), ['HELLO 123']);
    assert.deepEqual(
      result.diagnostics.recognitionBatchShapes.map((shape) => shape.computeUnit),
      ['gpu'],
    );
  } finally {
    await strict.close();
  }
});

test('decodes JPEG and PNG snapshots on the engine worker', async () => {
  const engine = await createEngine({ bundlePath });
  const png = Buffer.from(encodedBlankPng);
  const pngRecognition = engine.recognizeEncoded(png);
  png.fill(0);
  const pngResult = await pngRecognition;
  assert.equal(pngResult.imageWidth, 2);
  assert.equal(pngResult.imageHeight, 3);
  assert.deepEqual(pngResult.lines, []);
  assert.ok(Number.isSafeInteger(pngResult.timingUs.decode));

  const jpegResult = await engine.recognizeEncoded(encodedBlankJpeg);
  assert.equal(jpegResult.imageWidth, 2);
  assert.equal(jpegResult.imageHeight, 3);
  assert.deepEqual(jpegResult.lines, []);
  await engine.close();
});

test('matches raw recognition for a non-blank color PNG', async () => {
  const engine = await createEngine({ bundlePath });
  try {
    const image = loadFixture('paddleocr-garden-sign');
    const png = encodeBgrFixtureAsPng(image);
    const options = { includeDiagnostics: true };
    const rawResult = await engine.recognize(image, options);
    const encodedResult = await engine.recognizeEncoded(png, options);

    const stableResult = ({ timingUs, ...result }) => result;
    assert.deepEqual(stableResult(encodedResult), stableResult(rawResult));
    assert.deepEqual(rawResult.lines.map((line) => line.text), ['绿洲仕格维花园公寓']);
    assert.equal(rawResult.timingUs.decode, 0);
    assert.ok(encodedResult.timingUs.decode > 0);
  } finally {
    await engine.close();
  }
});

test('decodes encoded images concurrently across independent engines', async () => {
  const engines = await Promise.all([
    createEngine({ bundlePath }),
    createEngine({ bundlePath }),
  ]);
  try {
    const [pngResult, jpegResult] = await Promise.all([
      engines[0].recognizeEncoded(encodedBlankPng),
      engines[1].recognizeEncoded(encodedBlankJpeg),
    ]);
    assert.deepEqual([pngResult.imageWidth, pngResult.imageHeight], [2, 3]);
    assert.deepEqual([jpegResult.imageWidth, jpegResult.imageHeight], [2, 3]);
  } finally {
    await Promise.all(engines.map((engine) => engine.close()));
  }
});

test('rejects malformed and unsupported encoded images safely', async () => {
  const engine = await createEngine({ bundlePath });
  await assert.rejects(
    engine.recognizeEncoded(Buffer.from('not an image')),
    (error) => error instanceof OcrError && error.code === 'invalid_image',
  );
  await assert.rejects(
    engine.recognizeEncoded(new Uint8Array()),
    (error) => error instanceof OcrError && error.code === 'invalid_image',
  );
  await assert.rejects(
    engine.recognizeEncoded(new Uint16Array([1, 2, 3])),
    (error) => error instanceof OcrError && error.code === 'invalid_image',
  );
  const oversizedPng = Buffer.from(encodedBlankPng);
  oversizedPng.writeUInt32BE(engine.info.limits.maxWidth + 1, 16);
  await assert.rejects(
    engine.recognizeEncoded(oversizedPng),
    (error) => error instanceof OcrError && error.code === 'resource_limit_exceeded',
  );
  if (typeof SharedArrayBuffer === 'function') {
    await assert.rejects(
      engine.recognizeEncoded(new Uint8Array(new SharedArrayBuffer(16))),
      (error) => error instanceof OcrError && error.code === 'invalid_image',
    );
  }
  await engine.close();

  const limits = engine.info.limits;
  const memoryLimited = await createEngine({
    bundlePath,
    reducedLimits: {
      maxWidth: limits.maxWidth,
      maxHeight: limits.maxHeight,
      maxPixels: limits.maxPixels,
      maxDetectionSide: limits.maxDetectionSide,
      maxDetectionCandidates: limits.maxDetectionCandidates,
      maxRecognitionBatchSize: limits.maxRecognitionBatchSize,
      maxRecognitionWidth: limits.maxRecognitionWidth,
      // 18 decoded RGB bytes fit the old output-only check (18 <= 64 / 2),
      // but stb's decoder allocations do not fit this request-level budget.
      maxTemporaryBytes: 64,
    },
  });
  await assert.rejects(
    memoryLimited.recognizeEncoded(encodedBlankPng),
    (error) => error instanceof OcrError && error.code === 'resource_limit_exceeded',
  );
  await memoryLimited.close();
});

test('validates input and reports adapter errors as OcrError', async () => {
  await assert.rejects(
    createEngine({ model: 'ppocrv6-small', bundlePath }),
    (error) => error instanceof OcrError && error.code === 'invalid_argument',
  );
  await assert.rejects(
    createEngine({ model: 'unknown-model' }),
    (error) => error instanceof OcrError && error.code === 'invalid_argument',
  );
  await assert.rejects(
    createEngine({ bundlePath: path.join(repositoryRoot, 'models/does-not-exist') }),
    (error) => error instanceof OcrError && error.code === 'bundle_io_failed',
  );
  await assert.rejects(
    createEngine({ bundlePath, execution: { provider: 'coreml' } }),
    (error) => error instanceof OcrError && error.code === 'invalid_argument',
  );
  await assert.rejects(
    createEngine({ bundlePath, execution: { provider: 'apple' } }),
    (error) => error instanceof OcrError && error.code === 'unsupported_capability',
  );
  await assert.rejects(
    createEngine({ bundlePath, execution: { provider: 'webgpu' } }),
    (error) => {
      assert.ok(error instanceof OcrError);
      assert.equal(error.code, 'unsupported_capability');
      assert.equal(error.creationTrace, undefined);
      return true;
    },
  );
  await assert.rejects(
    createEngine({ bundlePath, execution: { precision: 'fp16' } }),
    (error) => error instanceof OcrError && error.code === 'invalid_argument',
  );
  await assert.rejects(
    createEngine({ bundlePath, execution: { deviceId: 0 } }),
    (error) => error instanceof OcrError && error.code === 'invalid_argument',
  );
  await assert.rejects(
    createEngine({ bundlePath, execution: { sessionFallback: 'cpu' } }),
    (error) => error instanceof OcrError && error.code === 'invalid_argument',
  );
  await assert.rejects(
    createEngine({ bundlePath, execution: { misspelledOption: true } }),
    (error) => error instanceof OcrError && error.code === 'invalid_argument',
  );

  const explicitCpu = await createEngine({
    bundlePath,
    execution: { provider: 'cpu', precision: 'fp32' },
  });
  assert.equal(explicitCpu.info.execution.requestedPrecision, 'fp32');
  assert.equal(explicitCpu.info.execution.sessions.detection.precision, 'fp32');
  await explicitCpu.close();

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

test('maps bounded request limits and explicit upstream-exact engine options', async () => {
  const legacyReducedLimits = await createEngine({
    bundlePath,
    reducedLimits: {
      maxWidth: 10_000,
      maxHeight: 10_000,
      maxPixels: 40_000_000,
      maxDetectionSide: 4_000,
      maxDetectionCandidates: 3_000,
      maxRecognitionBatchSize: 8,
      maxRecognitionWidth: 3_200,
      maxTemporaryBytes: 512 * 1024 * 1024,
    },
  });
  assert.equal(legacyReducedLimits.info.limits.maxDetectionTiles, 100);
  await legacyReducedLimits.close();

  const bounded = await createEngine({ bundlePath });
  const image = loadFixture('generated-hello-123');
  const boundedResult = await bounded.recognize(image, { detectionMaxSide: 640 });
  assert.deepEqual(boundedResult.lines.map((line) => line.text), ['HELLO 123']);
  await assert.rejects(
    bounded.recognize(image, { detectionMaxSide: 992 }),
    (error) => error instanceof OcrError && error.code === 'invalid_argument',
  );
  await bounded.close();

  const exact = await createEngine({
    bundlePath,
    detection: { strategy: 'upstreamExact' },
    recognitionBatchSize: 8,
  });
  assert.equal(exact.info.detectionStrategy, 'upstreamExact');
  assert.equal(exact.info.detectionMaxSide, 4000);
  assert.equal(exact.info.defaultRecognitionBatchSize, 8);
  await exact.close();

  await assert.rejects(
    createEngine({
      bundlePath,
      detection: { strategy: 'upstreamExact', maxSide: 960 },
    }),
    (error) => error instanceof OcrError && error.code === 'invalid_argument',
  );
});

test('maps tiled-v1 engine identity, passes, merge diagnostics, and side rejection', async () => {
  const tiled = await createEngine({
    bundlePath,
    detection: { strategy: 'tiled' },
  });
  assert.equal(tiled.info.detectionStrategy, 'tiled');
  assert.equal(tiled.info.detectionMaxSide, 1280);
  assert.equal(tiled.info.tiledDetection.contractVersion, 'tiled-v1');
  assert.equal(tiled.info.tiledDetection.tileSide, 1280);
  assert.equal(tiled.info.tiledDetection.minimumOverlap, 128);
  assert.equal(tiled.info.tiledDetection.artificialBoundaryMargin, 32);
  assert.equal(tiled.info.tiledDetection.mergeIouThreshold, 0.5);
  assert.ok(Math.abs(tiled.info.tiledDetection.mergeIosThreshold - 0.8) < 1e-6);

  const side = 2048;
  const image = {
    data: Buffer.alloc(side * side * 3, 255),
    width: side,
    height: side,
    stride: side * 3,
    pixelFormat: 'bgr8',
  };
  const result = await tiled.recognize(image, { includeDiagnostics: true });
  assert.deepEqual(result.lines, []);
  assert.equal(result.diagnostics.detectionPasses.length, 4);
  assert.deepEqual(
    result.diagnostics.detectionPasses.map(({ tileOrdinal, x, y }) => ({ tileOrdinal, x, y })),
    [
      { tileOrdinal: 0, x: 0, y: 0 },
      { tileOrdinal: 1, x: 768, y: 0 },
      { tileOrdinal: 2, x: 0, y: 768 },
      { tileOrdinal: 3, x: 768, y: 768 },
    ],
  );
  assert.equal(result.diagnostics.maxLiveDetectionPassBuffers, 1);
  assert.equal(result.diagnostics.rawDetectionBoxes, 0);
  assert.equal(result.diagnostics.suppressedDuplicateBoxes, 0);
  await assert.rejects(
    tiled.recognize(loadFixture('generated-blank'), { detectionMaxSide: 960 }),
    (error) => error instanceof OcrError && error.code === 'invalid_argument',
  );
  await tiled.close();
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
  await assert.rejects(
    oneSlot.recognizeEncoded(Buffer.from('not an image')),
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
