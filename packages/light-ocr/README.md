# @arcships/light-ocr

The default PP-OCRv6 Small entry for `light-ocr`. It exact-pins one compatible model-free runtime and one Small model package, and remains the only owner of the `light-ocr` command.

```bash
npm install @arcships/light-ocr
light-ocr info --version
```

`0.4.0` is the N2 topology cutover: the facade contains only Small model configuration and delegates the API, native loading, EXIF handling, and CLI implementation to the shared runtime. Tiny and Medium use separate packages and commands, so installing this package still installs only Small.
