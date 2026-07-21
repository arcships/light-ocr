'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');

const { createShutdownHandler } = require('../src/server');

test('shutdown handler closes the server, closes the engine, then exits', async () => {
  const calls = [];
  const fakeServer = { close: () => calls.push('server.close') };
  const fakeEngine = {
    close: async () => {
      calls.push('engine.close');
    },
  };
  const fakeExit = (code) => calls.push(`exit(${code})`);

  const shutdown = createShutdownHandler(fakeServer, fakeEngine, fakeExit);
  await shutdown();

  assert.deepEqual(calls, ['server.close', 'engine.close', 'exit(0)']);
});

test('shutdown handler only runs once when called twice concurrently', async () => {
  const calls = [];
  const fakeServer = { close: () => calls.push('server.close') };
  const fakeEngine = { close: async () => calls.push('engine.close') };
  const fakeExit = (code) => calls.push(`exit(${code})`);

  const shutdown = createShutdownHandler(fakeServer, fakeEngine, fakeExit);
  await Promise.all([shutdown(), shutdown()]);

  assert.deepEqual(calls, ['server.close', 'engine.close', 'exit(0)']);
});
