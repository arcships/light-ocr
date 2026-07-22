'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');

const { readConfig } = require('../src/config');

test('readConfig supplies conservative defaults', () => {
  assert.deepEqual(readConfig({}), {
    port: 3000,
    executionMode: 'cpu',
    queueCapacity: 4,
  });
});

test('readConfig accepts supported explicit values', () => {
  assert.deepEqual(
    readConfig({ PORT: '8080', EXECUTION_MODE: 'auto', QUEUE_CAPACITY: '8' }),
    { port: 8080, executionMode: 'auto', queueCapacity: 8 },
  );
});

test('readConfig rejects invalid integer and provider values', () => {
  assert.throws(() => readConfig({ PORT: '0' }), /PORT/);
  assert.throws(() => readConfig({ PORT: '3000.5' }), /PORT/);
  assert.throws(() => readConfig({ QUEUE_CAPACITY: '65' }), /QUEUE_CAPACITY/);
  assert.throws(() => readConfig({ EXECUTION_MODE: 'cuda' }), /EXECUTION_MODE/);
});
