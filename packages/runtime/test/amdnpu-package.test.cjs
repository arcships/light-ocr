'use strict';
const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const packageRoot = process.env.LIGHT_OCR_AMD_TEST_PACKAGE;
const bundlePath = process.env.LIGHT_OCR_AMD_TEST_BUNDLE;
const archive = process.env.LIGHT_OCR_AMD_TEST_ARCHIVE;
const unavailable = !packageRoot || !bundlePath;

// Run against generated artifacts on a host without an AMD NPU. Ordinary
// runtime tests remain dependency-free and skip these integration cases.
test('actual AMD addon loads, Auto uses CPU, and no-device policy is explicit', { skip: unavailable }, async t => {
  const previous = [process.env.LIGHT_OCR_NODE_BINARY, process.env.LIGHT_OCR_RUNTIME_DESCRIPTOR];
  t.after(() => {
    for (const [index, key] of ['LIGHT_OCR_NODE_BINARY', 'LIGHT_OCR_RUNTIME_DESCRIPTOR'].entries()) {
      if (previous[index] === undefined) delete process.env[key];
      else process.env[key] = previous[index];
    }
  });
  process.env.LIGHT_OCR_NODE_BINARY = path.resolve(packageRoot, 'native/light_ocr_node.node');
  process.env.LIGHT_OCR_RUNTIME_DESCRIPTOR = path.resolve(packageRoot, 'native/runtime-descriptor.json');
  const { createEngine } = require('../src/index.cjs');
  await assert.rejects(createEngine({ bundlePath, execution: { provider: 'amdnpu' } }),
    e => e.code === 'unsupported_capability' && /No accessible AMD/.test(e.message));
  await assert.rejects(createEngine({ bundlePath, execution: { provider: 'amdnpu', cpuPartition: 'forbid' } }),
    e => e.code === 'unsupported_capability' && /CPU detector partition/.test(e.message));
  await assert.rejects(createEngine({ bundlePath, execution: { provider: 'amdnpu', sessionFallback: 'cpu' } }),
    e => e.code === 'invalid_argument');
  const engine = await createEngine({ bundlePath, execution: { provider: 'auto' } });
  try {
    assert.equal(engine.info.execution.selectionTrace.selectedProvider, 'cpu');
    assert.deepEqual(engine.info.execution.selectionTrace.orderedCandidates, ['cpu']);
    assert.deepEqual(engine.info.execution.sessions.recognition.actualProviderChain, ['CPUExecutionProvider']);
    const result = await engine.recognize({ width: 64, height: 48, stride: 64,
      pixelFormat: 'gray8', data: new Uint8Array(64 * 48).fill(255) });
    assert.ok(result);
  } finally { await engine.close(); }
});

test('AMD tarball installs offline and production resolution reaches no-device error',
  { skip: unavailable || !archive }, () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'light-ocr-amd-install-'));
    try {
      fs.writeFileSync(path.join(root, 'package.json'), JSON.stringify({ name: 'amd-offline-test', version: '1.0.0', private: true }));
      const installed = spawnSync('npm', ['install', '--offline', '--ignore-scripts', '--no-audit', '--no-fund',
        '--cache', path.join(root, 'cache'), '--prefix', root, path.resolve(archive)], { encoding: 'utf8' });
      assert.equal(installed.status, 0, installed.stderr);
      const runtime = path.join(root, 'node_modules/@arcships/light-ocr-runtime');
      fs.mkdirSync(runtime, { recursive: true });
      fs.cpSync(path.resolve(__dirname, '../src'), path.join(runtime, 'src'), { recursive: true });
      fs.copyFileSync(path.resolve(__dirname, '../package.json'), path.join(runtime, 'package.json'));
      // npm_release.stage replaces the workspace VERSION lookup with this
      // literal metadata in published runtime packages.
      const coreVersion = require('../src/metadata.cjs').coreVersion;
      fs.writeFileSync(path.join(runtime, 'src/metadata.cjs'),
        `'use strict';\nmodule.exports = Object.freeze({ coreVersion: ${JSON.stringify(coreVersion)} });\n`);
      const env = { ...process.env };
      delete env.LIGHT_OCR_NODE_BINARY; delete env.LIGHT_OCR_RUNTIME_DESCRIPTOR;
      const probe = spawnSync(process.execPath, ['-e', `
        const assert=require('node:assert/strict');
        const {createEngine}=require(${JSON.stringify(runtime)});
        createEngine({bundlePath:${JSON.stringify(path.resolve(bundlePath))},execution:{provider:'amdnpu'}})
          .then(async engine=>{await engine.close();throw new Error('Unexpected NPU device');})
          .catch(error=>{assert.equal(error.code,'unsupported_capability');assert.match(error.message,/No accessible AMD/);});
      `], { env, encoding: 'utf8', timeout: 30000 });
      assert.equal(probe.status, 0, probe.stderr);
    } finally { fs.rmSync(root, { recursive: true, force: true }); }
  });
