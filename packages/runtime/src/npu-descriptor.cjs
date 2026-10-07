'use strict';

const path = require('node:path');
const fs = require('node:fs');

const WIDTHS = Object.freeze([320, 384, 480, 544, 576, 608, 704, 736, 832, 960,
  1056, 1184, 1248, 1376, 1600, 1984, 2240, 2560, 2880, 3200]);

function validateNpuProviders(descriptor, root, { exactKeys, verifyArtifact, sameArtifact, adapterError }) {
  const paths = new Set();
  const providers = {};
  const fail = (message) => { throw adapterError('package_load_failed', message); };
  for (const name of ['openvino', 'amdnpu']) {
    const provider = descriptor.providers[name];
    if (!provider) continue;
    if (descriptor.schemaVersion !== '2.1' || descriptor.platform.id !== 'linux-x64') {
      fail('NPU providers require schema 2.1 and Linux x64 glibc');
    }
    exactKeys(provider, ['runtimeProvider', 'providerVersion', 'qualificationId',
      'providerLibrary', 'configuration', 'artifacts'], `providers.${name}`);
    const expected = name === 'openvino' ? ['OpenVINO'] : ['VitisAIExecutionProvider', 'IREEAMDAIE'];
    if (!expected.includes(provider.runtimeProvider) || typeof provider.providerVersion !== 'string' ||
        !provider.providerVersion || typeof provider.qualificationId !== 'string' ||
        !provider.qualificationId || !Array.isArray(provider.artifacts) || !provider.artifacts.length) {
      fail('Invalid NPU provider identity');
    }
    const declaredPaths = new Set();
    for (const artifact of provider.artifacts) {
      verifyArtifact(root, artifact, `providers.${name}.artifacts`);
      if (!artifact.path.startsWith(`native/${name}/`) || declaredPaths.has(artifact.path) ||
          !descriptor.runtime.artifacts.some((item) => sameArtifact(item, artifact))) {
        fail('NPU artifact is outside its provider inventory');
      }
      declaredPaths.add(artifact.path);
      paths.add(artifact.path);
    }
    const library = verifyArtifact(root, provider.providerLibrary, `providers.${name}.providerLibrary`);
    if (!provider.artifacts.some((item) => sameArtifact(item, provider.providerLibrary))) {
      fail('NPU library is outside its provider inventory');
    }
    const configuration = provider.configuration;
    let nativeConfiguration;
    if (name === 'openvino') {
      exactKeys(configuration, ['runtimeVersionPrefix', 'minimumDriverVersion', 'minimumCompilerVersion'],
        'OpenVINO configuration');
      if (typeof configuration.runtimeVersionPrefix !== 'string' || !configuration.runtimeVersionPrefix ||
          !/^[0-9]+$/.test(configuration.minimumDriverVersion) ||
          typeof configuration.minimumDriverVersion !== 'string' ||
          !/^[0-9]+$/.test(configuration.minimumCompilerVersion) ||
          typeof configuration.minimumCompilerVersion !== 'string') {
        fail('Invalid OpenVINO numeric driver/compiler floors');
      }
      const expectedNames = ['libopenvino.so.2640', 'libopenvino_c.so.2640',
        'libopenvino_intel_npu_plugin.so', 'libopenvino_onnx_frontend.so.2640', 'libtbb.so.12'].sort();
      const names = [...declaredPaths].map((item) => path.basename(item)).sort();
      if (path.basename(library) !== 'libopenvino_c.so.2640' ||
          names.length !== expectedNames.length || names.some((item, i) => item !== expectedNames[i])) {
        fail('Incomplete OpenVINO NPU dependency closure');
      }
      nativeConfiguration = Object.freeze({ ...configuration });
    } else {
      exactKeys(configuration, ['sourceModelSha256', 'recognitionModels', 'compilerConfiguration'], 'AMD NPU configuration');
      const configPath = verifyArtifact(root, configuration.compilerConfiguration, 'AMD NPU compiler configuration');
      if (!provider.artifacts.some((item) => sameArtifact(item, configuration.compilerConfiguration))) {
        fail('AMD NPU compiler configuration is outside its inventory');
      }
      if (typeof configuration.sourceModelSha256 !== 'string' ||
          !/^[0-9a-f]{64}$/.test(configuration.sourceModelSha256) ||
          !Array.isArray(configuration.recognitionModels) || configuration.recognitionModels.length !== WIDTHS.length ||
          !(provider.runtimeProvider === 'IREEAMDAIE'
            ? path.basename(library) === 'liblight_ocr_amdaie.so.1'
            : path.basename(library).startsWith('libonnxruntime.so'))) {
        fail('Invalid AMD NPU recognition model identity');
      }
      const modelPaths = new Set();
      const models = configuration.recognitionModels.map((model, i) => {
        exactKeys(model, ['width', 'artifact'], 'AMD NPU recognition bucket');
        const modelPath = verifyArtifact(root, model.artifact, 'AMD NPU recognition bucket artifact');
        if (model.width !== WIDTHS[i] || modelPaths.has(modelPath) ||
            !provider.artifacts.some((item) => sameArtifact(item, model.artifact))) {
          fail('Incomplete AMD NPU recognition buckets');
        }
        modelPaths.add(modelPath);
        return Object.freeze({ width: model.width, artifact: Object.freeze({
          path: modelPath, bytes: model.artifact.bytes, sha256: model.artifact.sha256,
        }) });
      });
      if (provider.runtimeProvider === 'IREEAMDAIE') {
        let compiler;
        try { compiler = JSON.parse(fs.readFileSync(configPath, 'utf8')); }
        catch { fail('Invalid AMD AIE deployment configuration'); }
        if (compiler.target !== 'IREEAMDAIE' || compiler.runtimeAbi !== 1 ||
            compiler.device !== 'npu4' || compiler.sourceModelSha256 !== configuration.sourceModelSha256 ||
            compiler.parameterScope !== 'recognition' || compiler.precision !== 'bf16-bfp16ebs8' ||
            compiler.cpuPartitionRequired !== true || typeof compiler.runtimeVersion !== 'string' ||
            !compiler.runtimeVersion || !Array.isArray(compiler.widths) ||
            compiler.widths.length !== WIDTHS.length || compiler.widths.some((width, i) => width !== WIDTHS[i]) ||
            configuration.recognitionModels.some((item) => !item.artifact.path.endsWith('.vmfb'))) {
          fail('AMD AIE deployment contract differs from the locked recognition model');
        }
        const parameters = compiler.parameters;
        if (!parameters || typeof parameters.path !== 'string' || !parameters.path ||
            parameters.path.includes('\\') || path.posix.isAbsolute(parameters.path) ||
            parameters.path.split('/').some((component) => !component || component === '.' || component === '..')) {
          fail('Invalid AMD shared parameter path');
        }
        const artifact = { ...parameters,
          path: path.posix.join(path.posix.dirname(configuration.compilerConfiguration.path), parameters.path) };
        verifyArtifact(root, artifact, 'AMD shared recognition parameters');
        if (!provider.artifacts.some((item) => sameArtifact(item, artifact))) {
          fail('AMD shared recognition parameters are outside the runtime inventory');
        }
      }
      nativeConfiguration = Object.freeze({ sourceModelSha256: configuration.sourceModelSha256,
        compilerConfiguration: Object.freeze({ path: configPath,
          bytes: configuration.compilerConfiguration.bytes, sha256: configuration.compilerConfiguration.sha256 }),
        recognitionModels: Object.freeze(models) });
    }
    providers[name] = Object.freeze({ providerLibrary: library,
      providerBytes: provider.providerLibrary.bytes, providerSha256: provider.providerLibrary.sha256,
      configuration: nativeConfiguration });
  }
  return { paths, providers: Object.freeze(providers), names: Object.keys(providers) };
}

module.exports = { validateNpuProviders, WIDTHS };
