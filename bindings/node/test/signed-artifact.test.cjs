'use strict';

// Coverage for the macOS-only signed-artifact relaxation in
// packages/runtime/src/load-native.cjs: a Mach-O whose bytes/sha256 differ
// from the runtime descriptor is accepted on darwin when its code signature
// verifies and its signing identity matches the host process (same
// TeamIdentifier, or both ad-hoc). All other platforms keep the strict
// bytes+sha256 gate, and unsigned mutations are always rejected.
//
// The release gate runs this suite once with the default Node host and once
// with an ad-hoc re-signed Node copy, so both the different-identity rejection
// and mutual ad-hoc acceptance paths execute on macOS. The real Developer ID
// round-trip (same team as the host) additionally runs when a matching
// code-signing identity is available in the keychain and usable without an
// interactive prompt; otherwise that one case self-skips and remains manual
// release evidence.

const assert = require('node:assert/strict');
const { spawnSync } = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');

const { validateRuntimeDescriptor, macOSSignature } = require(
  '../../../packages/runtime/src/load-native.cjs',
);

const onDarwin = process.platform === 'darwin';

function run(command, args, options) {
  const result = spawnSync(command, args, { encoding: 'utf8', ...options });
  return { status: result.status, stdout: result.stdout, stderr: result.stderr, error: result.error };
}

function codesign(args, options) {
  return run('codesign', args, options);
}

function signAdHoc(filename) {
  return codesign(['--force', '-s', '-', filename]).status === 0;
}

function flipByte(filename, offset) {
  const bytes = fs.readFileSync(filename);
  bytes[offset] ^= 0xff;
  fs.writeFileSync(filename, bytes);
}

function appendByte(filename) {
  const bytes = fs.readFileSync(filename);
  fs.writeFileSync(filename, Buffer.concat([bytes, Buffer.from([0x00])]));
}

function assertPackageLoadFailed(callback, messagePart) {
  assert.throws(
    callback,
    (error) => (
      error.name === 'OcrError' &&
      error.code === 'package_load_failed' &&
      (messagePart === undefined || error.message.includes(messagePart))
    ),
  );
}

// Stages a minimal macos native/ payload whose addon and runtime are real
// Mach-O copies (universal /bin/ls), with a matching runtime descriptor.
function stagePackage() {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'light-ocr-signed-'));
  const native = path.join(directory, 'native');
  fs.mkdirSync(native);
  const addon = path.join(native, 'light_ocr_node.node');
  const runtime = path.join(native, 'libonnxruntime.1.22.0.dylib');
  fs.copyFileSync('/bin/ls', addon);
  fs.copyFileSync('/bin/ls', runtime);
  const record = (filename) => ({
    path: path.relative(directory, filename).replaceAll(path.sep, '/'),
    bytes: fs.statSync(filename).size,
    sha256: crypto.createHash('sha256').update(fs.readFileSync(filename)).digest('hex'),
  });
  const platformId = `macos-${process.arch}`;
  const machine = process.arch === 'arm64' ? 'arm64' : 'x86_64';
  const appleSupported = process.arch === 'arm64';
  const descriptor = {
    schemaVersion: '2.0',
    platform: { id: platformId, os: 'darwin', architecture: machine },
    runtime: {
      flavor: 'cpu',
      kind: 'onnxruntime-cpu',
      version: '1.22.0',
      abi: 'onnxruntime-c-api-22',
      artifacts: [record(runtime)],
    },
    qualificationOnly: false,
    released: true,
    autoPolicy: {
      id: `${platformId}-v1`,
      version: 1,
      providers: appleSupported ? ['apple', 'cpu'] : ['cpu'],
    },
    providers: {
      cpu: {
        runtimeProvider: 'CPUExecutionProvider',
        qualificationId: 'cpu-baseline-v1',
        artifacts: [record(runtime)],
      },
      ...(appleSupported
        ? {
            apple: {
              runtimeProvider: 'CoreML',
              qualificationId: 'apple-open-macos-v1',
              artifacts: [record(addon)],
            },
          }
        : {}),
    },
    addon: record(addon),
  };
  const descriptorPath = path.join(native, 'runtime-descriptor.json');
  fs.writeFileSync(descriptorPath, `${JSON.stringify(descriptor)}\n`);
  return { directory, addon, runtime, descriptorPath };
}

test('signerMatchesHost accepts only identical teams or mutual ad-hoc', () => {
  const teamA = { teamIdentifier: 'AAAAAAAAAA', adhoc: false };
  const teamB = { teamIdentifier: 'BBBBBBBBBB', adhoc: false };
  const adhoc = { teamIdentifier: null, adhoc: true };
  assert.equal(macOSSignature.signerMatchesHost(teamA, teamA), true);
  assert.equal(macOSSignature.signerMatchesHost(teamB, teamB), true);
  assert.equal(macOSSignature.signerMatchesHost(adhoc, adhoc), true);
  assert.equal(macOSSignature.signerMatchesHost(teamA, teamB), false);
  assert.equal(macOSSignature.signerMatchesHost(teamA, adhoc), false);
  assert.equal(macOSSignature.signerMatchesHost(adhoc, teamA), false);
  assert.equal(macOSSignature.signerMatchesHost(null, teamA), false);
  assert.equal(macOSSignature.signerMatchesHost(teamA, null), false);
  assert.equal(macOSSignature.signerMatchesHost(adhoc, null), false);
  assert.equal(macOSSignature.signerMatchesHost(null, null), false);
});

test(
  'detects Mach-O files and parses codesign identity output',
  { skip: !onDarwin },
  () => {
    const { directory } = stagePackage();
    try {
      const macho = path.join(directory, 'native', 'libonnxruntime.1.22.0.dylib');
      assert.equal(macOSSignature.isMachO(macho), true);
      const text = path.join(directory, 'text.bin');
      fs.writeFileSync(text, 'not a mach-o');
      assert.equal(macOSSignature.isMachO(text), false);
      assert.equal(signAdHoc(macho), true);
      assert.equal(macOSSignature.codesignVerifies(macho), true);
      assert.deepEqual(macOSSignature.codesignIdentity(macho), {
        teamIdentifier: null,
        adhoc: true,
      });
    } finally {
      fs.rmSync(directory, { recursive: true, force: true });
    }
  },
);

test(
  'accepts pristine descriptor artifacts on macOS',
  { skip: !onDarwin },
  () => {
    const { directory, addon, descriptorPath } = stagePackage();
    try {
      assert.equal(validateRuntimeDescriptor(descriptorPath).addon, addon);
    } finally {
      fs.rmSync(directory, { recursive: true, force: true });
    }
  },
);

test(
  'rejects a mutated artifact that was not re-signed',
  { skip: !onDarwin },
  () => {
    const { directory, runtime, addon, descriptorPath } = stagePackage();
    try {
      appendByte(runtime); // size divergence
      assertPackageLoadFailed(() => validateRuntimeDescriptor(descriptorPath), 'byte count');
      flipByte(addon, 0x10); // hash divergence
      assertPackageLoadFailed(() => validateRuntimeDescriptor(descriptorPath), 'hash');
    } finally {
      fs.rmSync(directory, { recursive: true, force: true });
    }
  },
);

test(
  'rejects a malformed digest before considering the signed-mutation fallback',
  { skip: !onDarwin },
  () => {
    const { directory, addon, runtime, descriptorPath } = stagePackage();
    try {
      assert.equal(signAdHoc(runtime), true);
      const descriptor = JSON.parse(fs.readFileSync(descriptorPath, 'utf8'));
      descriptor.runtime.artifacts[0].bytes = fs.statSync(runtime).size - 1;
      descriptor.runtime.artifacts[0].sha256 = 'not-a-sha256-digest';
      fs.writeFileSync(descriptorPath, `${JSON.stringify(descriptor)}\n`);
      assertPackageLoadFailed(() => validateRuntimeDescriptor(descriptorPath), 'hash');
    } finally {
      fs.rmSync(directory, { recursive: true, force: true });
    }
  },
);

test(
  'rejects a mutated artifact re-signed ad-hoc by a different identity',
  { skip: !onDarwin },
  (t) => {
    const host = macOSSignature.codesignIdentity(process.execPath);
    if (host?.adhoc) {
      t.skip('host process is ad-hoc signed; ad-hoc re-signing is the matching identity');
      return;
    }
    const { directory, runtime, descriptorPath } = stagePackage();
    try {
      flipByte(runtime, Math.floor(fs.statSync(runtime).size / 2));
      assert.equal(signAdHoc(runtime), true);
      assertPackageLoadFailed(() => validateRuntimeDescriptor(descriptorPath));
    } finally {
      fs.rmSync(directory, { recursive: true, force: true });
    }
  },
);

test(
  'accepts a mutated artifact re-signed ad-hoc when the host is ad-hoc signed',
  { skip: !onDarwin },
  (t) => {
    const host = macOSSignature.codesignIdentity(process.execPath);
    if (!host?.adhoc) {
      t.skip('host process is not ad-hoc signed');
      return;
    }
    const { directory, addon, runtime, descriptorPath } = stagePackage();
    try {
      flipByte(runtime, Math.floor(fs.statSync(runtime).size / 2));
      assert.equal(signAdHoc(runtime), true);
      assert.equal(validateRuntimeDescriptor(descriptorPath).addon, addon);
    } finally {
      fs.rmSync(directory, { recursive: true, force: true });
    }
  },
);

test(
  'rejects a mutated non-Mach-O artifact even when ad-hoc signed',
  { skip: !onDarwin },
  () => {
    const { directory, runtime, descriptorPath } = stagePackage();
    try {
      fs.writeFileSync(runtime, 'mutated font-like payload');
      assert.equal(signAdHoc(runtime), true);
      assertPackageLoadFailed(() => validateRuntimeDescriptor(descriptorPath));
    } finally {
      fs.rmSync(directory, { recursive: true, force: true });
    }
  },
);

test(
  'accepts a mutated artifact re-signed with the host Developer ID',
  { skip: !onDarwin },
  (t) => {
    const host = macOSSignature.codesignIdentity(process.execPath);
    if (!host || host.adhoc) {
      t.skip('host process is not Developer ID signed');
      return;
    }
    const identities = run('security', ['find-identity', '-v', '-p', 'codesigning']);
    if (identities.status !== 0) {
      t.skip('security tooling unavailable');
      return;
    }
    const match = identities.stdout.match(
      new RegExp(`^\\s*\\d+\\)\\s+([0-9A-F]+)\\s+\\"([^\\"]+)\\(${host.teamIdentifier}\\)\\"\\s*$`, 'm'),
    );
    if (!match) {
      t.skip(`no keychain identity for team ${host.teamIdentifier}`);
      return;
    }
    const { directory, addon, runtime, descriptorPath } = stagePackage();
    try {
      flipByte(runtime, Math.floor(fs.statSync(runtime).size / 2));
      // The keychain may require interactive authorization; give codesign a
      // short budget and self-skip instead of hanging the suite.
      const signed = codesign(['--force', '-s', match[1], runtime], { timeout: 5000 });
      if (signed.status !== 0) {
        t.skip(`Developer ID signing unavailable: ${signed.stderr || signed.error?.message}`);
        return;
      }
      assert.deepEqual(macOSSignature.codesignIdentity(runtime), {
        teamIdentifier: host.teamIdentifier,
        adhoc: false,
      });
      assert.equal(validateRuntimeDescriptor(descriptorPath).addon, addon);
    } finally {
      fs.rmSync(directory, { recursive: true, force: true });
    }
  },
);
