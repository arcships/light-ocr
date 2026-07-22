'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');

const { createShutdownHandler } = require('../src/server');

test('shutdown drains HTTP, closes the engine, then exits', async () => {
  const calls = [];
  const fakeServer = {
    close(callback) {
      calls.push('server.close');
      callback();
    },
  };
  const fakeEngine = { close: async () => calls.push('engine.close') };
  const shutdown = createShutdownHandler(
    fakeServer,
    fakeEngine,
    (code) => calls.push(`exit(${code})`),
  );

  await shutdown();
  assert.deepEqual(calls, ['server.close', 'engine.close', 'exit(0)']);
});

test('shutdown only runs once when called concurrently', async () => {
  const calls = [];
  const fakeServer = {
    close(callback) {
      calls.push('server.close');
      callback();
    },
  };
  const fakeEngine = { close: async () => calls.push('engine.close') };
  const shutdown = createShutdownHandler(
    fakeServer,
    fakeEngine,
    (code) => calls.push(`exit(${code})`),
  );

  await Promise.all([shutdown(), shutdown()]);
  assert.deepEqual(calls, ['server.close', 'engine.close', 'exit(0)']);
});

test('shutdown waits for HTTP drain before closing the engine', async () => {
  const calls = [];
  let finishDrain;
  const fakeServer = {
    close(callback) {
      calls.push('server.close:start');
      finishDrain = () => {
        calls.push('server.close:done');
        callback();
      };
    },
  };
  const fakeEngine = { close: async () => calls.push('engine.close') };
  const shutdown = createShutdownHandler(
    fakeServer,
    fakeEngine,
    (code) => calls.push(`exit(${code})`),
  );

  const shutdownPromise = shutdown();
  await Promise.resolve();
  assert.deepEqual(calls, ['server.close:start']);

  finishDrain();
  await shutdownPromise;
  assert.deepEqual(calls, [
    'server.close:start',
    'server.close:done',
    'engine.close',
    'exit(0)',
  ]);
});
