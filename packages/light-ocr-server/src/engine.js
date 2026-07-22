'use strict';

const { createEngine } = require('@arcships/light-ocr');
const { readConfig } = require('./config');

let enginePromise = null;

function initEngine(config = readConfig()) {
  if (!enginePromise) {
    enginePromise = createEngine({
      queueCapacity: config.queueCapacity,
      execution: { provider: config.executionMode },
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
