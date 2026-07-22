# @arcships/light-ocr-runtime

Model-free Node.js runtime for `light-ocr`. It owns native loading, engine lifecycle, encoded-image support, scheduling, errors, and shared OCR types.

The N2 release assembler publishes this package as the shared runtime. Direct callers must provide a local model bundle:

```js
const { createEngine } = require('@arcships/light-ocr-runtime');

const engine = await createEngine({ bundlePath: '/absolute/model/bundle' });
```

Use `@arcships/light-ocr` for stable Small, `@arcships/light-ocr-tiny` for the size-first preview, or `@arcships/light-ocr-medium` for the quality-first preview. The runtime never downloads a model.
