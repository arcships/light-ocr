'use strict';

const EXECUTION_MODES = new Set(['auto', 'cpu', 'apple', 'webgpu']);

function readInteger(name, value, fallback, minimum, maximum) {
  const raw = value ?? String(fallback);
  if (!/^\d+$/.test(raw)) {
    throw new Error(`${name} must be an integer from ${minimum} through ${maximum}`);
  }
  const parsed = Number(raw);
  if (!Number.isSafeInteger(parsed) || parsed < minimum || parsed > maximum) {
    throw new Error(`${name} must be an integer from ${minimum} through ${maximum}`);
  }
  return parsed;
}

function readConfig(env = process.env) {
  const executionMode = env.EXECUTION_MODE ?? 'cpu';
  if (!EXECUTION_MODES.has(executionMode)) {
    throw new Error('EXECUTION_MODE must be one of: auto, cpu, apple, webgpu');
  }
  return Object.freeze({
    port: readInteger('PORT', env.PORT, 3000, 1, 65535),
    executionMode,
    queueCapacity: readInteger('QUEUE_CAPACITY', env.QUEUE_CAPACITY, 4, 1, 64),
  });
}

module.exports = { readConfig };
