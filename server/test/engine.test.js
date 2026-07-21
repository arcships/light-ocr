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

test('initEngine resolves an engine using the cpu provider by default', async () => {
  delete process.env.EXECUTION_MODE;
  const engine = await initEngine();
  assert.equal(engine.info.execution.requestedProvider, 'cpu');
});

test('getEngine returns the same promise as initEngine after initialization', async () => {
  const first = await initEngine();
  const second = await getEngine();
  assert.equal(first, second);
});
