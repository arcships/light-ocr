'use strict';
const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { validateNpuProviders, WIDTHS } = require('../src/npu-descriptor.cjs');

function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'light-ocr-amd-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const artifacts = [];
  const write = (relative, bytes) => {
    const destination = path.join(root, relative);
    fs.mkdirSync(path.dirname(destination), { recursive: true });
    fs.writeFileSync(destination, bytes);
    const record = { path: relative, bytes: Buffer.byteLength(bytes),
      sha256: crypto.createHash('sha256').update(bytes).digest('hex') };
    artifacts.push(record);
    return record;
  };
  const library = write('native/amdnpu/lib/liblight_ocr_amdaie.so.1', 'test library');
  const parameters = write('native/amdnpu/models/recognition.irpa', 'test shared weights');
  const compiler = { target: 'IREEAMDAIE', runtimeAbi: 1, device: 'npu4',
    sourceModelSha256: 'a'.repeat(64), parameterScope: 'recognition',
    precision: 'bf16-bfp16ebs8', cpuPartitionRequired: true, runtimeVersion: 'pinned-test',
    widths: [...WIDTHS], parameters: { ...parameters, path: 'models/recognition.irpa' } };
  const configuration = write('native/amdnpu/deployment.json', JSON.stringify(compiler));
  const provider = { runtimeProvider: 'IREEAMDAIE', providerVersion: 'pinned-test',
    qualificationId: 'opt-in-unqualified', providerLibrary: library,
    configuration: { sourceModelSha256: compiler.sourceModelSha256, compilerConfiguration: configuration,
      recognitionModels: WIDTHS.map(width => ({ width,
        artifact: write(`native/amdnpu/models/recognition-${width}.vmfb`, `test bucket ${width}`) })) }, artifacts };
  const descriptor = { schemaVersion: '2.1', platform: { id: 'linux-x64' },
    runtime: { artifacts }, providers: { amdnpu: provider } };
  const error = (code, message) => Object.assign(new Error(message), { code });
  const callbacks = {
    adapterError: error,
    exactKeys(value, expected) {
      if (!value || typeof value !== 'object' || Array.isArray(value) ||
          Object.keys(value).sort().join() !== [...expected].sort().join()) {
        throw error('package_load_failed', 'Unexpected keys');
      }
    },
    sameArtifact(a, b) { return a.path === b.path && a.bytes === b.bytes && a.sha256 === b.sha256; },
    verifyArtifact(base, artifact) {
      if (!artifact || typeof artifact.path !== 'string' || artifact.path.split('/').includes('..')) {
        throw error('package_load_failed', 'Invalid path');
      }
      let bytes;
      try { bytes = fs.readFileSync(path.join(base, artifact.path)); }
      catch { throw error('package_load_failed', 'Missing artifact'); }
      if (bytes.length !== artifact.bytes || crypto.createHash('sha256').update(bytes).digest('hex') !== artifact.sha256) {
        throw error('package_load_failed', 'Artifact identity differs');
      }
      return path.join(base, artifact.path);
    },
  };
  return { root, provider, descriptor, compiler,
    validate: () => validateNpuProviders(descriptor, root, callbacks),
    replaceCompiler(value) {
      const bytes = JSON.stringify(value);
      fs.writeFileSync(path.join(root, configuration.path), bytes);
      configuration.bytes = Buffer.byteLength(bytes);
      configuration.sha256 = crypto.createHash('sha256').update(bytes).digest('hex');
    } };
}
const rejected = f => assert.throws(f.validate, e => e.code === 'package_load_failed');

test('AMD AIE accepts 20 buckets and shared weights with the unchanged native configuration shape', t => {
  const f = fixture(t); const result = f.validate();
  assert.deepEqual(result.names, ['amdnpu']);
  assert.equal(result.providers.amdnpu.configuration.recognitionModels.length, 20);
  assert.deepEqual(Object.keys(result.providers.amdnpu.configuration).sort(),
    ['compilerConfiguration', 'recognitionModels', 'sourceModelSha256']);
  assert.ok(Object.isFrozen(result.providers.amdnpu.configuration));
});
for (const [name, change] of [
  ['missing bucket', f => f.provider.configuration.recognitionModels.pop()],
  ['reordered bucket', f => f.provider.configuration.recognitionModels.reverse()],
  ['duplicate bucket path', f => { f.provider.configuration.recognitionModels[1].artifact = f.provider.configuration.recognitionModels[0].artifact; }],
  ['wrong runtime library', f => { f.provider.runtimeProvider = 'VitisAIExecutionProvider'; }],
  ['wrong ABI', f => { f.compiler.runtimeAbi = 2; f.replaceCompiler(f.compiler); }],
  ['wrong source identity', f => { f.compiler.sourceModelSha256 = 'b'.repeat(64); f.replaceCompiler(f.compiler); }],
  ['CPU partition disabled', f => { f.compiler.cpuPartitionRequired = false; f.replaceCompiler(f.compiler); }],
  ['traversal in shared weight path', f => { f.compiler.parameters.path = '../outside.irpa'; f.replaceCompiler(f.compiler); }],
  ['absolute shared weight path', f => { f.compiler.parameters.path = '/tmp/outside.irpa'; f.replaceCompiler(f.compiler); }],
  ['shared weights missing from inventory', f => { f.provider.artifacts.splice(f.provider.artifacts.findIndex(a => a.path.endsWith('.irpa')), 1); }],
  ['corrupt shared weights', f => fs.appendFileSync(path.join(f.root, 'native/amdnpu/models/recognition.irpa'), 'corrupt')],
  ['missing shared weights', f => fs.unlinkSync(path.join(f.root, 'native/amdnpu/models/recognition.irpa'))],
  ['null deployment JSON', f => f.replaceCompiler(null)],
  ['array deployment JSON', f => f.replaceCompiler([])],
]) test(`AMD AIE rejects ${name}`, t => { const f = fixture(t); change(f); rejected(f); });
