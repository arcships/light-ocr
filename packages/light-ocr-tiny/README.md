# @arcships/light-ocr-tiny

Size-first preview facade for light-ocr. It exposes the same Node.js API and
CLI schema as `@arcships/light-ocr`, but uses the PP-OCRv6 Tiny bundle.

Tiny covers 49 languages and does not support Japanese (`ja`). It remains on
the `next` channel until the N2 quality and resource gate is complete.

```js
const { createEngine } = require('@arcships/light-ocr-tiny');
const engine = await createEngine();
```

The CLI command is `light-ocr-tiny`; the stable `light-ocr` command remains
owned exclusively by the Small package.
