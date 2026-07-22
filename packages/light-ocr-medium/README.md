# @arcships/light-ocr-medium

Quality-first preview facade for light-ocr. It exposes the same Node.js API and
CLI schema as `@arcships/light-ocr`, but uses the larger PP-OCRv6 Medium bundle.

Medium remains on the `next` channel until its quality gain justifies its model
size, memory, and startup cost under the N2 gate.

```js
const { createEngine } = require('@arcships/light-ocr-medium');
const engine = await createEngine();
```

The CLI command is `light-ocr-medium`; the stable `light-ocr` command remains
owned exclusively by the Small package.
