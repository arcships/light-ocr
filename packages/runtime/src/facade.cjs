'use strict';

const fs = require('node:fs');
const path = require('node:path');

const runtime = require('./index.cjs');

function createModelFacade(config) {
  if (!config || typeof config !== 'object') {
    throw new TypeError('model facade config must be an object');
  }
  const {
    model,
    modelPackage,
    compatibleBundleIds,
    appleBundleIds = compatibleBundleIds,
    developmentBundlePath,
    profile,
  } = config;
  if (typeof model !== 'string' || model === '') {
    throw new TypeError('model facade config requires model');
  }
  if (typeof modelPackage !== 'string' || modelPackage === '') {
    throw new TypeError('model facade config requires modelPackage');
  }
  if (!Array.isArray(compatibleBundleIds) || compatibleBundleIds.length === 0) {
    throw new TypeError('model facade config requires compatibleBundleIds');
  }
  if (!profile || typeof profile !== 'object' || profile.model !== model) {
    throw new TypeError('model facade config requires a matching profile');
  }

  function resolveBuiltInBundle(selectedModel, requireApple) {
    if (selectedModel !== model) {
      throw new runtime.OcrError(
        'invalid_argument',
        `model must be ${JSON.stringify(model)}`,
      );
    }

    let manifestPath;
    if (developmentBundlePath) {
      const candidate = path.resolve(developmentBundlePath, 'manifest.json');
      if (fs.existsSync(candidate)) manifestPath = candidate;
    }
    if (!manifestPath) {
      try {
        manifestPath = require.resolve(`${modelPackage}/bundle/manifest.json`);
      } catch (cause) {
        throw new runtime.OcrError(
          'package_load_failed',
          `Unable to locate the built-in ${model} model`,
          `Reinstall ${modelPackage}; ${cause instanceof Error ? cause.message : String(cause)}`,
        );
      }
    }

    let manifest;
    try {
      manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
    } catch (cause) {
      throw new runtime.OcrError(
        'package_load_failed',
        'Unable to read the built-in model manifest',
        cause instanceof Error ? cause.message : String(cause),
      );
    }
    const accepted = requireApple ? appleBundleIds : compatibleBundleIds;
    if (!accepted.includes(manifest.bundleId)) {
      throw new runtime.OcrError(
        'package_load_failed',
        `The installed model package is incompatible with the ${model} facade`,
        `expected ${accepted.join(' or ')}, received ${String(manifest.bundleId)}`,
      );
    }
    return path.dirname(manifestPath);
  }

  function resolveCreateOptions(options) {
    if (options === undefined) options = {};
    if (options === null || typeof options !== 'object' || Array.isArray(options)) {
      throw new runtime.OcrError('invalid_argument', 'createEngine options must be an object');
    }
    const hasModel = Object.prototype.hasOwnProperty.call(options, 'model');
    const hasBundlePath = Object.prototype.hasOwnProperty.call(options, 'bundlePath');
    if (hasModel && hasBundlePath) {
      throw new runtime.OcrError(
        'invalid_argument',
        'model and bundlePath cannot be used together',
      );
    }
    if (hasBundlePath) return options;
    const selectedModel = hasModel ? options.model : model;
    const requireApple = options.execution?.provider === 'apple';
    const resolved = {
      ...options,
      bundlePath: resolveBuiltInBundle(selectedModel, requireApple),
    };
    delete resolved.model;
    return resolved;
  }

  async function createEngine(options) {
    return runtime.createEngine(resolveCreateOptions(options));
  }

  const modelProfile = Object.freeze({
    ...profile,
    excludedLanguages: Object.freeze([...(profile?.excludedLanguages || [])]),
  });

  return Object.freeze({
    createEngine,
    OcrError: runtime.OcrError,
    modelProfile,
  });
}

module.exports = { createModelFacade };
