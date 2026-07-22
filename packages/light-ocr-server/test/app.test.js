'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { after, before, test } = require('node:test');

const { createApp } = require('../src/app');
const { initEngine } = require('../src/engine');

let engine;
let server;
let baseUrl;

before(async () => {
  engine = await initEngine({ executionMode: 'cpu', queueCapacity: 4 });
  const app = createApp(engine, { maxConcurrentUploads: 4 });
  server = app.listen(0);
  await new Promise((resolve) => server.once('listening', resolve));
  baseUrl = `http://127.0.0.1:${server.address().port}`;
});

after(async () => {
  await new Promise((resolve) => server.close(resolve));
  await engine.close();
});

test('GET /api/v1/health returns 200 ok', async () => {
  const response = await fetch(`${baseUrl}/api/v1/health`);
  assert.equal(response.status, 200);
  assert.deepEqual(await response.json(), { status: 'ok' });
});

test('GET /api/v1/info returns execution info and version', async () => {
  const response = await fetch(`${baseUrl}/api/v1/info`);
  assert.equal(response.status, 200);
  const body = await response.json();
  assert.equal(body.execution.requestedProvider, 'cpu');
  assert.equal(body.version, '0.1.0');
});

test('POST /api/v1/ocr recognizes text in a real image', async () => {
  const imagePath = path.resolve(__dirname, 'fixtures/hello-123.png');
  const imageBuffer = fs.readFileSync(imagePath);
  const form = new FormData();
  form.set('image', new Blob([imageBuffer], { type: 'image/png' }), 'hello-123.png');

  const response = await fetch(`${baseUrl}/api/v1/ocr`, { method: 'POST', body: form });
  assert.equal(response.status, 200);
  const body = await response.json();
  assert.ok(Array.isArray(body.lines));
  assert.ok(body.lines.some((line) => /HELLO/i.test(line.text)));
});

test('POST /api/v1/ocr without a file returns 400', async () => {
  const response = await fetch(`${baseUrl}/api/v1/ocr`, {
    method: 'POST',
    body: new FormData(),
  });
  assert.equal(response.status, 400);
  assert.equal((await response.json()).error, 'missing_image');
});

test('POST /api/v1/ocr with non-image data returns 422', async () => {
  const form = new FormData();
  form.set(
    'image',
    new Blob([Buffer.from('not an image')], { type: 'application/octet-stream' }),
    'garbage.bin',
  );

  const response = await fetch(`${baseUrl}/api/v1/ocr`, { method: 'POST', body: form });
  assert.equal(response.status, 422);
  assert.equal((await response.json()).error, 'invalid_image');
});
