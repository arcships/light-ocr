'use strict';

const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

function adapterError(code, message, detail, cause) {
  const error = new Error(message, cause === undefined ? undefined : { cause });
  error.name = 'OcrError';
  error.code = code;
  if (detail) error.detail = detail;
  return error;
}

function platformIdentity() {
  const key = `${process.platform}-${process.arch}`;
  const identities = {
    'darwin-arm64': { id: 'macos-arm64', os: 'darwin', architecture: 'arm64' },
    'darwin-x64': { id: 'macos-x64', os: 'darwin', architecture: 'x86_64' },
    'win32-x64': { id: 'windows-x64', os: 'win32', architecture: 'x86_64' },
  };
  if (key === 'linux-x64') {
    const report = process.report?.getReport?.();
    if (report?.header?.glibcVersionRuntime) {
      return { id: 'linux-x64', os: 'linux', architecture: 'x86_64', libc: 'glibc' };
    }
    throw adapterError(
      'unsupported_platform',
      'light-ocr currently supports Linux x64 with glibc only',
      key,
    );
  }
  const identity = identities[key];
  if (!identity) {
    throw adapterError('unsupported_platform', `light-ocr does not support ${key}`, key);
  }
  return identity;
}

function platformPackage() {
  const packages = {
    'macos-arm64': '@arcships/light-ocr-darwin-arm64',
    'macos-x64': '@arcships/light-ocr-darwin-x64',
    'windows-x64': '@arcships/light-ocr-win32-x64',
    'linux-x64': '@arcships/light-ocr-linux-x64-gnu',
  };
  return packages[platformIdentity().id];
}

function exactKeys(value, expected, field) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    throw adapterError('package_load_failed', `${field} must be an object`);
  }
  const actual = Object.keys(value).sort();
  const wanted = [...expected].sort();
  if (actual.length !== wanted.length || actual.some((key, index) => key !== wanted[index])) {
    throw adapterError('package_load_failed', `${field} fields are invalid`);
  }
}

function safeArtifactPath(root, value, field) {
  if (typeof value !== 'string' || value === '' || value.includes('\0')) {
    throw adapterError('package_load_failed', `${field} must be a package-relative path`);
  }
  const normalized = value.replaceAll('\\', '/');
  if (
    path.posix.isAbsolute(normalized) ||
    /^[A-Za-z]:/.test(normalized) ||
    normalized.split('/').some((part) => part === '..' || part === '' || part === '.')
  ) {
    throw adapterError('package_load_failed', `${field} escapes the native package`, value);
  }
  const resolved = path.resolve(root, ...normalized.split('/'));
  const relative = path.relative(path.resolve(root), resolved);
  if (relative.startsWith('..') || path.isAbsolute(relative)) {
    throw adapterError('package_load_failed', `${field} escapes the native package`, value);
  }
  return resolved;
}

function sha256(filename) {
  return crypto.createHash('sha256').update(fs.readFileSync(filename)).digest('hex');
}

function verifyArtifact(root, artifact, field) {
  exactKeys(artifact, ['path', 'bytes', 'sha256'], field);
  const filename = safeArtifactPath(root, artifact.path, `${field}.path`);
  let stats;
  try {
    stats = fs.lstatSync(filename);
  } catch (cause) {
    throw adapterError('package_load_failed', 'Descriptor artifact is missing', artifact.path, cause);
  }
  if (!stats.isFile() || stats.isSymbolicLink()) {
    throw adapterError('package_load_failed', 'Descriptor artifact is not a regular file', artifact.path);
  }
  if (!Number.isSafeInteger(artifact.bytes) || artifact.bytes < 1 || stats.size !== artifact.bytes) {
    throw adapterError('package_load_failed', 'Descriptor artifact byte count mismatch', artifact.path);
  }
  if (!/^[a-f0-9]{64}$/.test(artifact.sha256 || '') || sha256(filename) !== artifact.sha256) {
    throw adapterError('package_load_failed', 'Descriptor artifact hash mismatch', artifact.path);
  }
  return filename;
}

function sameArtifact(left, right) {
  return left?.path === right?.path && left?.bytes === right?.bytes &&
    left?.sha256 === right?.sha256;
}

function validateRuntimeDescriptor(descriptorPath) {
  const absoluteDescriptor = path.resolve(descriptorPath);
  const nativeDirectory = path.dirname(absoluteDescriptor);
  if (
    path.basename(absoluteDescriptor) !== 'runtime-descriptor.json' ||
    path.basename(nativeDirectory) !== 'native'
  ) {
    throw adapterError(
      'package_load_failed',
      'Native runtime descriptor must use the package native/runtime-descriptor.json path',
      absoluteDescriptor,
    );
  }
  let descriptor;
  try {
    const nativeStats = fs.lstatSync(nativeDirectory);
    if (!nativeStats.isDirectory() || nativeStats.isSymbolicLink()) {
      throw new Error('native payload root is not a regular directory');
    }
    const stats = fs.lstatSync(absoluteDescriptor);
    if (!stats.isFile() || stats.isSymbolicLink()) throw new Error('not a regular file');
    descriptor = JSON.parse(fs.readFileSync(absoluteDescriptor, 'utf8'));
  } catch (cause) {
    throw adapterError(
      'package_load_failed',
      'Unable to read the native runtime descriptor',
      absoluteDescriptor,
      cause,
    );
  }
  exactKeys(
    descriptor,
    ['schemaVersion', 'platform', 'runtime', 'qualificationOnly', 'released',
      'autoPolicy', 'providers', 'addon'],
    'runtime descriptor',
  );
  if (descriptor.schemaVersion !== '2.0') {
    throw adapterError('package_load_failed', 'Unsupported native runtime descriptor schema');
  }
  const root = path.dirname(path.dirname(absoluteDescriptor));
  const expected = platformIdentity();
  const expectedPlatformKeys = expected.libc
    ? ['id', 'os', 'architecture', 'libc']
    : ['id', 'os', 'architecture'];
  exactKeys(descriptor.platform, expectedPlatformKeys, 'platform');
  const actual = descriptor.platform;
  if (
    actual.id !== expected.id ||
    actual.os !== expected.os ||
    actual.architecture !== expected.architecture ||
    (expected.libc && actual.libc !== expected.libc)
  ) {
    throw adapterError('package_load_failed', 'Native runtime descriptor platform mismatch');
  }
  if (
    typeof descriptor.released !== 'boolean' ||
    typeof descriptor.qualificationOnly !== 'boolean' ||
    descriptor.qualificationOnly === descriptor.released
  ) {
    throw adapterError('package_load_failed', 'Native runtime descriptor release flags are invalid');
  }

  exactKeys(descriptor.autoPolicy, ['id', 'version', 'providers'], 'autoPolicy');
  const policy = descriptor.autoPolicy;
  if (
    typeof policy.id !== 'string' || policy.id === '' ||
    !Number.isSafeInteger(policy.version) || policy.version < 1 || policy.version > 0xffffffff ||
    !Array.isArray(policy.providers) || policy.providers.length === 0 ||
    policy.providers.length > 3 || policy.providers.at(-1) !== 'cpu' ||
    new Set(policy.providers).size !== policy.providers.length
  ) {
    throw adapterError('package_load_failed', 'Native runtime descriptor Auto policy is invalid');
  }

  exactKeys(descriptor.runtime, ['flavor', 'kind', 'version', 'abi', 'artifacts'], 'runtime');
  const runtime = descriptor.runtime;
  const expectedRuntimes = {
    cpu: { kind: 'onnxruntime-cpu', version: '1.22.0', abi: 'onnxruntime-c-api-22' },
    webgpu: {
      kind: 'onnxruntime-plugin-webgpu',
      version: '1.24.4',
      abi: 'onnxruntime-c-api-24-plugin-ep-0.1',
    },
  };
  const expectedRuntime = expectedRuntimes[runtime.flavor];
  if (
    !expectedRuntime || runtime.kind !== expectedRuntime.kind ||
    runtime.version !== expectedRuntime.version || runtime.abi !== expectedRuntime.abi ||
    !Array.isArray(runtime.artifacts) || runtime.artifacts.length === 0
  ) {
    throw adapterError('package_load_failed', 'Native runtime descriptor ABI identity is invalid');
  }
  if (runtime.flavor === 'webgpu' && !['linux', 'win32'].includes(actual.os)) {
    throw adapterError('package_load_failed', 'WebGPU runtime is not supported on this platform');
  }
  if (runtime.flavor !== 'webgpu' && descriptor.qualificationOnly) {
    throw adapterError('package_load_failed', 'CPU runtime cannot be qualification-only');
  }

  const addon = verifyArtifact(root, descriptor.addon, 'addon');
  const runtimePaths = new Set();
  const verifiedRuntime = new Map();
  runtime.artifacts.forEach((artifact, index) => {
    const filename = verifyArtifact(root, artifact, `runtime.artifacts[${index}]`);
    if (runtimePaths.has(artifact.path)) {
      throw adapterError('package_load_failed', 'Runtime artifact inventory contains a duplicate path');
    }
    runtimePaths.add(artifact.path);
    verifiedRuntime.set(artifact.path, filename);
  });

  exactKeys(descriptor.providers, Object.keys(descriptor.providers), 'providers');
  const availableProviders = Object.keys(descriptor.providers);
  if (
    availableProviders.length === 0 ||
    new Set(availableProviders).size !== availableProviders.length ||
    availableProviders.some((provider) => !['cpu', 'apple', 'webgpu'].includes(provider)) ||
    !descriptor.providers.cpu
  ) {
    throw adapterError('package_load_failed', 'Native runtime descriptor provider policy is invalid');
  }
  let webgpuLibrary = '';
  let webgpuProviderBytes = 0;
  let webgpuProviderSha256 = '';
  for (const [providerId, provider] of Object.entries(descriptor.providers)) {
    const expectedKeys = providerId === 'webgpu'
      ? ['runtimeProvider', 'providerVersion', 'qualificationId', 'providerLibrary', 'artifacts']
      : ['runtimeProvider', 'qualificationId', 'artifacts'];
    exactKeys(provider, expectedKeys, `providers.${providerId}`);
    if (
      typeof provider.qualificationId !== 'string' || provider.qualificationId === '' ||
      !Array.isArray(provider.artifacts) || provider.artifacts.length === 0
    ) {
      throw adapterError('package_load_failed', `Provider ${providerId} identity is invalid`);
    }
    const expectedProviderName = {
      cpu: 'CPUExecutionProvider',
      apple: 'CoreML',
      webgpu: 'WebGpuExecutionProvider',
    }[providerId];
    if (provider.runtimeProvider !== expectedProviderName) {
      throw adapterError('package_load_failed', `Provider ${providerId} runtime identity is invalid`);
    }
    const providerPaths = new Set();
    provider.artifacts.forEach((artifact, index) => {
      verifyArtifact(root, artifact, `providers.${providerId}.artifacts[${index}]`);
      if (providerPaths.has(artifact.path)) {
        throw adapterError('package_load_failed', `Provider ${providerId} has duplicate artifacts`);
      }
      providerPaths.add(artifact.path);
      if (providerId !== 'apple' && !runtimePaths.has(artifact.path)) {
        throw adapterError('package_load_failed', `Provider ${providerId} artifact is outside runtime inventory`);
      }
      if (providerId === 'apple' && artifact.path !== descriptor.addon.path) {
        throw adapterError('package_load_failed', 'Apple provider artifact must be the native addon');
      }
    });
    if (providerId === 'webgpu') {
      if (provider.providerVersion !== '0.1.0') {
        throw adapterError('package_load_failed', 'WebGPU provider version is invalid');
      }
      webgpuLibrary = verifyArtifact(root, provider.providerLibrary, 'providers.webgpu.providerLibrary');
      const declared = provider.artifacts.find(
        (artifact) => artifact.path === provider.providerLibrary.path,
      );
      const expectedBasename = actual.os === 'win32'
        ? 'onnxruntime_providers_webgpu.dll'
        : 'libonnxruntime_providers_webgpu.so';
      if (
        !declared || !sameArtifact(declared, provider.providerLibrary) ||
        path.basename(webgpuLibrary) !== expectedBasename
      ) {
        throw adapterError('package_load_failed', 'WebGPU provider library contract is invalid');
      }
      webgpuProviderBytes = provider.providerLibrary.bytes;
      webgpuProviderSha256 = provider.providerLibrary.sha256;
    }
  }

  const coreName = actual.os === 'win32'
    ? 'onnxruntime.dll'
    : actual.os === 'darwin'
      ? 'libonnxruntime.1.22.0.dylib'
      : 'libonnxruntime.so.1';
  const runtimeNames = [...verifiedRuntime.values()].map((filename) => path.basename(filename)).sort();
  const expectedRuntimeNames = runtime.flavor === 'webgpu'
    ? actual.os === 'win32'
      ? ['dxcompiler.dll', 'dxil.dll', 'onnxruntime.dll', 'onnxruntime_providers_webgpu.dll']
      : ['libonnxruntime.so.1', 'libonnxruntime_providers_webgpu.so']
    : [coreName];
  if (
    runtimeNames.length !== expectedRuntimeNames.length ||
    runtimeNames.some((name, index) => name !== expectedRuntimeNames[index])
  ) {
    throw adapterError('package_load_failed', 'Native runtime artifact set is incomplete');
  }
  const cpuArtifacts = descriptor.providers.cpu.artifacts;
  if (
    cpuArtifacts.length !== 1 ||
    path.basename(safeArtifactPath(root, cpuArtifacts[0].path, 'CPU artifact path')) !== coreName
  ) {
    throw adapterError('package_load_failed', 'CPU provider does not reference the core runtime');
  }

  const actualPayload = new Set();
  const inventory = (directory, relativeDirectory) => {
    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
      const relative = `${relativeDirectory}/${entry.name}`;
      const filename = path.join(directory, entry.name);
      if (entry.isSymbolicLink()) {
        throw adapterError('package_load_failed', 'Native runtime payload contains a symlink', relative);
      }
      if (entry.isDirectory()) {
        inventory(filename, relative);
      } else if (entry.isFile() && relative !== 'native/runtime-descriptor.json') {
        actualPayload.add(relative);
      } else if (!entry.isFile()) {
        throw adapterError('package_load_failed', 'Native runtime payload is not a regular file', relative);
      }
    }
  };
  inventory(nativeDirectory, 'native');
  const referenced = new Set([descriptor.addon.path, ...runtimePaths]);
  if (
    actualPayload.size !== referenced.size ||
    [...actualPayload].some((filename) => !referenced.has(filename))
  ) {
    throw adapterError('package_load_failed', 'Native runtime descriptor payload inventory mismatch');
  }

  const expectedPolicy = runtime.flavor === 'webgpu'
    ? ['webgpu', 'cpu']
    : actual.os === 'darwin'
      ? ['apple', 'cpu']
      : ['cpu'];
  const expectedAvailable = runtime.flavor === 'webgpu'
    ? ['cpu', 'webgpu']
    : actual.os === 'darwin'
      ? ['apple', 'cpu']
      : ['cpu'];
  const sortedAvailable = [...availableProviders].sort();
  if (
    policy.providers.length !== expectedPolicy.length ||
    policy.providers.some((provider, index) => provider !== expectedPolicy[index]) ||
    sortedAvailable.length !== expectedAvailable.length ||
    sortedAvailable.some((provider, index) => provider !== expectedAvailable[index]) ||
    policy.providers.some((provider) => !availableProviders.includes(provider))
  ) {
    throw adapterError(
      'package_load_failed',
      'Native runtime descriptor providers disagree with platform capabilities',
    );
  }
  const providerQualificationIds = sortedAvailable.map(
    (providerId) => descriptor.providers[providerId].qualificationId,
  );
  const runtimePolicy = Object.freeze({
    id: policy.id,
    version: policy.version,
    platformId: actual.id,
    runtimeFlavor: runtime.flavor,
    runtimeVersion: runtime.version,
    runtimeAbi: runtime.abi,
    qualificationOnly: descriptor.qualificationOnly,
    released: descriptor.released,
    orderedCandidates: Object.freeze([...policy.providers]),
    availableProviders: Object.freeze(sortedAvailable),
    providerQualificationIds: Object.freeze(providerQualificationIds),
    webgpuProviderLibrary: webgpuLibrary,
    webgpuProviderBytes,
    webgpuProviderSha256,
  });
  return {
    addon,
    descriptor: Object.freeze(descriptor),
    descriptorPath: absoluteDescriptor,
    runtimePolicy,
  };
}

function resolveDevelopmentInput() {
  if (!process.env.LIGHT_OCR_NODE_BINARY) return undefined;
  const binary = path.resolve(process.env.LIGHT_OCR_NODE_BINARY);
  const descriptor = process.env.LIGHT_OCR_RUNTIME_DESCRIPTOR
    ? path.resolve(process.env.LIGHT_OCR_RUNTIME_DESCRIPTOR)
    : path.join(path.dirname(binary), 'runtime-descriptor.json');
  return { binary, descriptor };
}

function validateNativeContract(binding, runtimePolicy) {
  const contract = binding?.runtimeContract;
  const fields = [
    'policyId',
    'policyVersion',
    'platformId',
    'runtimeFlavor',
    'runtimeVersion',
    'runtimeAbi',
    'qualificationOnly',
    'released',
  ];
  const policyFields = {
    policyId: runtimePolicy.id,
    policyVersion: runtimePolicy.version,
    platformId: runtimePolicy.platformId,
    runtimeFlavor: runtimePolicy.runtimeFlavor,
    runtimeVersion: runtimePolicy.runtimeVersion,
    runtimeAbi: runtimePolicy.runtimeAbi,
    qualificationOnly: runtimePolicy.qualificationOnly,
    released: runtimePolicy.released,
  };
  if (
    !contract ||
    typeof contract !== 'object' ||
    fields.some((field) => contract[field] !== policyFields[field]) ||
    !Array.isArray(contract.orderedCandidates) ||
    !Array.isArray(contract.availableProviders) ||
    !Array.isArray(contract.providerQualificationIds) ||
    contract.orderedCandidates.length !== runtimePolicy.orderedCandidates.length ||
    contract.orderedCandidates.some(
      (provider, index) => provider !== runtimePolicy.orderedCandidates[index],
    ) ||
    contract.availableProviders.length !== runtimePolicy.availableProviders.length ||
    contract.availableProviders.some(
      (provider, index) => provider !== runtimePolicy.availableProviders[index],
    ) ||
    contract.providerQualificationIds.length !== runtimePolicy.providerQualificationIds.length ||
    contract.providerQualificationIds.some(
      (qualificationId, index) => qualificationId !== runtimePolicy.providerQualificationIds[index],
    )
  ) {
    throw adapterError(
      'package_load_failed',
      'Runtime descriptor is incompatible with the native addon ABI or capabilities',
    );
  }
}

function loadNative() {
  const development = resolveDevelopmentInput();
  let input;
  if (development) {
    input = development;
  } else {
    const packageName = platformPackage();
    try {
      const binary = require.resolve(packageName);
      input = { binary, descriptor: path.join(path.dirname(binary), 'runtime-descriptor.json') };
    } catch (cause) {
      throw adapterError(
        'package_load_failed',
        `Unable to locate ${packageName}`,
        'Reinstall @arcships/light-ocr without --omit=optional and verify that the current platform is supported.',
        cause,
      );
    }
  }

  if (!fs.existsSync(input.binary)) {
    throw adapterError('package_load_failed', 'Native addon is missing', input.binary);
  }
  const verified = validateRuntimeDescriptor(input.descriptor);
  if (path.resolve(input.binary) !== path.resolve(verified.addon)) {
    throw adapterError('package_load_failed', 'Runtime descriptor addon path mismatch', input.binary);
  }
  try {
    const binding = require(verified.addon);
    validateNativeContract(binding, verified.runtimePolicy);
    return Object.freeze({ binding, runtimePolicy: verified.runtimePolicy });
  } catch (cause) {
    throw adapterError('package_load_failed', 'Unable to load the verified native addon', '', cause);
  }
}

module.exports = { loadNative, validateRuntimeDescriptor };
