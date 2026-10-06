# @arcships/light-ocr-runtime

Model-free Node.js runtime for `light-ocr`. It owns native loading, engine lifecycle, encoded-image support, scheduling, errors, and shared OCR types.

The N2 release assembler publishes this package as the shared runtime. Direct callers must provide a local model bundle:

```js
const { createEngine } = require('@arcships/light-ocr-runtime');

const engine = await createEngine({ bundlePath: '/absolute/model/bundle' });
```

Use `@arcships/light-ocr` for stable Small, `@arcships/light-ocr-tiny` for the size-first preview, or `@arcships/light-ocr-medium` for the quality-first preview. The runtime never downloads a model.

## NPU configuration

The default install contains no AMD or OpenVINO libraries or compiled NPU models. Small keeps its existing locked model bundle. Install the matching support package separately:

```bash
npm install @arcships/light-ocr-openvino-linux-x64-gnu@0.5.9
# Or, for AMD STX/KRK and Small 0.3.4:
npm install @arcships/light-ocr-amdnpu-linux-x64-gnu@0.5.9
```

```js
const engine = await createEngine({
  bundlePath: '/absolute/model/bundle',
  execution: { provider: 'amdnpu' }, // or 'openvino'
});
```

Both packages are optional peers, without automatic installation. Only an explicit provider selects their separate native addon. Auto uses the base package's existing WebGPU/Apple/CPU policy, even when support packages are installed. Explicit NPU selection never silently falls back if its package is absent.

Vendor drivers are required on Linux x64 glibc. AMD contexts bind to Small 0.3.4; Tiny/Medium need separately compiled contexts. Hardware inference has not been qualified; `deviceValidated` stays false.
