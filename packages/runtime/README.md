# @arcships/light-ocr-runtime

Model-free Node.js runtime for `light-ocr`. It owns native loading, engine lifecycle, encoded-image support, scheduling, errors, and shared OCR types.

The N2 release assembler publishes this package as the shared runtime. Direct callers must provide a local model bundle:

```js
const { createEngine } = require('@arcships/light-ocr-runtime');

const engine = await createEngine({ bundlePath: '/absolute/model/bundle' });
```

Use `@arcships/light-ocr` for stable Small, `@arcships/light-ocr-tiny` for the size-first preview, or `@arcships/light-ocr-medium` for the quality-first preview. The runtime never downloads a model.

## NPU configuration

Starting with the prepared 0.5.9 native release, Linux x64 glibc packages include Intel OpenVINO in Auto ahead of WebGPU/CPU. AMD is excluded from Auto and requires a build containing the vendor SDK and matching compiled models:

```js
const engine = await createEngine({
  bundlePath: '/absolute/model/bundle',
  execution: { provider: 'amdnpu' },
});
```

A package without AMD payload reports `unsupported_capability`. Explicit selection does not silently fall back. NPU shipping does not assert hardware qualification; `deviceValidated` stays false.
