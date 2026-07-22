'use strict';

const assert = require('node:assert/strict');
const { after, test } = require('node:test');

const { initEngine, getEngine } = require('../src/engine');

after(async () => {
  const engine = await getEngine();
  await engine.close();
});

test('getEngine throws before initEngine has been called', () => {
  assert.throws(() => getEngine(), /Engine not initialized/);
});

test('initEngine resolves a CPU engine from explicit configuration', async () => {
  const engine = await initEngine({ executionMode: 'cpu', queueCapacity: 4 });
  assert.equal(engine.info.execution.requestedProvider, 'cpu');
});

test('getEngine returns the initialized engine', async () => {
  const first = await initEngine();
  const second = await getEngine();
  assert.equal(first, second);
});
