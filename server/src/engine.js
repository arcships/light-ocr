'use strict';

const { createEngine } = require('@arcships/light-ocr');

let enginePromise = null;

function initEngine() {
  if (!enginePromise) {
    enginePromise = createEngine({
      queueCapacity: Number(process.env.QUEUE_CAPACITY ?? 4),
      execution: { provider: process.env.EXECUTION_MODE ?? 'cpu' },
    });
  }
  return enginePromise;
}

function getEngine() {
  if (!enginePromise) {
    throw new Error('Engine not initialized; call initEngine() first');
  }
  return enginePromise;
}

module.exports = { initEngine, getEngine };
