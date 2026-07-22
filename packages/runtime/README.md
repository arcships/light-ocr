# @arcships/light-ocr-runtime

Model-free Node.js runtime for `light-ocr`. It owns native loading, engine lifecycle, encoded-image support, scheduling, errors, and shared OCR types.

This N2 stage-1 package is private while the monorepo migration is validated. Direct callers must provide a local model bundle:

```js
const { createEngine } = require('@arcships/light-ocr-runtime');

const engine = await createEngine({ bundlePath: '/absolute/model/bundle' });
```

Use `@arcships/light-ocr` for the default PP-OCRv6 Small model and the `light-ocr` CLI. The runtime never downloads a model.
