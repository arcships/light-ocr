'use strict';

const assert = require('node:assert/strict');
const { after, before, test } = require('node:test');
const express = require('express');

const { errorHandler } = require('../src/errors');
const ocrRouter = require('../src/routes/ocr');

let server;
let baseUrl;
let recognizeStarted;
let releaseRecognize;

function armRecognition() {
  return new Promise((resolve) => {
    recognizeStarted = resolve;
  });
}

function imageForm(name) {
  const form = new FormData();
  form.set(
    'image',
    new Blob([Buffer.from('fake-image-bytes')], { type: 'image/png' }),
    name,
  );
  return form;
}

before(async () => {
  const fakeEngine = {
    recognizeEncoded: () => {
      recognizeStarted();
      return new Promise((resolve) => {
        releaseRecognize = () => resolve({ lines: [] });
      });
    },
  };
  const app = express();
  app.use(ocrRouter(fakeEngine, { maxConcurrentUploads: 1 }));
  app.use(errorHandler);
  server = app.listen(0);
  await new Promise((resolve) => server.once('listening', resolve));
  baseUrl = `http://127.0.0.1:${server.address().port}`;
});

after(async () => {
  await new Promise((resolve) => server.close(resolve));
});

test('POST /ocr rejects excess uploads and releases the slot', async () => {
  let started = armRecognition();
  const firstRequest = fetch(`${baseUrl}/ocr`, {
    method: 'POST',
    body: imageForm('a.png'),
  });
  await started;

  const secondResponse = await fetch(`${baseUrl}/ocr`, {
    method: 'POST',
    body: imageForm('b.png'),
  });
  assert.equal(secondResponse.status, 429);
  assert.equal((await secondResponse.json()).error, 'too_many_uploads');

  releaseRecognize();
  assert.equal((await firstRequest).status, 200);

  started = armRecognition();
  const thirdRequest = fetch(`${baseUrl}/ocr`, {
    method: 'POST',
    body: imageForm('c.png'),
  });
  await started;
  releaseRecognize();
  assert.equal((await thirdRequest).status, 200);
});
