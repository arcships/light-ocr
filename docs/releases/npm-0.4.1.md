# npm 0.4.1 N3 发布记录

状态：准备中（未发布）<br>
日期：2026-07-24

## 发布身份

- Release run：待定（CI 未触发）。
- 发布集合：1 个版本 bump（`@arcships/light-ocr@0.4.1`）+ 1 个新 preview
  package（`@arcships/light-ocr-document@0.1.0`）。其余 native/runtime/model
  package 版本不变。
- 本次不重新打包 native 闭包或模型；Small model 仍复用 `0.3.4`，runtime
  仍为 `0.1.0`。

## 范围

`0.4.1` 完成 N3 文档入口功能。Small facade 新增 `recognizeDocument()` API
和 `light-ocr document` CLI 子命令，支持 PDF 和多页图片的统一文档 OCR。PDF
渲染通过 `pdfium-native`（lazy-loaded、optional）实现；当 PDFium 不可用时
`hasPdfSupport()` 返回 `false`，API 以明确的 `unsupported_capability` 错误
降级，不影响已有的单图 `recognize` 路径。

| 角色 | package/version | 变化 | channel |
| --- | --- | --- | --- |
| Stable facade | `@arcships/light-ocr@0.4.1` | + `recognizeDocument` / `hasPdfSupport` / `document` CLI | `next` → `latest` |
| Document preview | `@arcships/light-ocr-document@0.1.0` | 新增独立文档引擎 | `next`；preview |
| Shared runtime | `@arcships/light-ocr-runtime@0.1.0` | 无变化 | 不变 |
| Native runtime | 六个 platform packages `0.4.0` | 无变化 | 不变 |
| Small model | `@arcships/light-ocr-model-ppocrv6-small@0.3.4` | 无变化 | 不变 |

## N3 功能详情

### `recognizeDocument()` API

```js
const { createEngine, recognizeDocument, hasPdfSupport } = require('@arcships/light-ocr');

// 单个 PDF 文件
for await (const page of recognizeDocument('/path/to/file.pdf', { dpi: 200 })) {
  console.log(page.index, page.lines.length, page.source.kind);
}

// 多页图片
for await (const page of recognizeDocument([buf1, buf2, buf3])) {
  console.log(page.index, page.lines);
}

// 手动 engine 注入（避免重复初始化）
const engine = await createEngine();
for await (const page of recognizeDocument('report.pdf', { engine, pageRange: { start: 2, end: 5 } })) {
  // ...
}
await engine.close();
```

每个 yielded page result 结构：

```jsonc
{
  "index": 0,
  "width": 1240,
  "height": 1754,
  "coordinateSpace": "pageSpace",
  "structure": "ocr-order",
  "lines": [{ "id": "L0", "text": "...", "confidence": 0.97, "box": { "x": 0, "y": 0, "w": 100, "h": 20 } }],
  "source": {
    "kind": "pdf",
    "mediaType": "application/pdf",
    "identity": { "pageIndex": 0 },
    "appliedTransforms": { "pdf": { "rotation": 0, "dpi": 150, "scale": 2.083, "mediaBox": { ... }, "cropBox": { ... } } }
  },
  "timingUs": { "total": 123000, "decode": 45000, "ocr": 78000 }
}
```

### `light-ocr document` CLI

```bash
light-ocr document --source report.pdf --dpi 200 --format text
light-ocr document --source scan.pdf --page-range 1-5 --format json
light-ocr document --source img1.png img2.png img3.png --format json
light-ocr document --source big.pdf --max-pages 50 --abort-on-limit
```

### 资源限制

| 参数 | 默认值 | 说明 |
| --- | ---: | --- |
| `maxPages` | 100 | 单次处理最大页数 |
| `maxPagePixels` | 16,777,216 (4096²) | 单页像素上限 |
| `maxTotalPixels` | 104,857,600 (100 MiB) | 所有页面累计像素上限 |
| `maxFileBytes` | 104,857,600 (100 MiB) | PDF 文件字节上限 |

超限抛出 `OcrError('resource_limit_exceeded', ...)`。

### `@arcships/light-ocr-document` 独立包

独立文档引擎，适用于不需要完整 `light-ocr` facade 的场景：

```js
const { createDocumentEngine, hasPdfSupport } = require('@arcships/light-ocr-document');

const docEngine = await createDocumentEngine();
for await (const page of docEngine.recognizeDocument('paper.pdf')) {
  console.log(page.lines.map(l => l.text).join('\n'));
}
await docEngine.close();
```

- 版本：`0.1.0`（preview）
- 依赖：`pdfium-native@0.6.1`（direct）
- Peer dependencies：`@arcships/light-ocr-runtime@^0.1.0`、`@arcships/light-ocr-model-ppocrv6-small@^0.3.4`（均 optional）

## 与 N2 闭包的关系

`0.4.1` 不改变 native、runtime 或 model package。六平台 native 仍为 `0.4.0`，
runtime 仍为 `0.1.0`，Small model 仍为 `0.3.4`。本次发布仅修改 facade 层
（`@arcships/light-ocr` package 内容 + 版本号）并新增独立 document preview
package。用户的安装命令不变：

```bash
npm install @arcships/light-ocr@0.4.1   # facade + runtime + native + model
npm install @arcships/light-ocr-document@0.1.0  # 独立文档引擎（preview）
```

## 待完成

以下项在实际发布前必须完成：

- [ ] CI release run 成功
- [ ] 六平台 Small facade install + OCR smoke
- [ ] `light-ocr document` 命令 smoke（PDF + 多页图片）
- [ ] `hasPdfSupport()` 在无 PDFium 环境返回 `false`
- [ ] npm registry `@arcships/light-ocr@0.4.1` integrity 验证
- [ ] `@arcships/light-ocr-document@0.1.0` 发布到 `next` tag

## 兼容性

- 向后兼容：`0.4.0` 的所有 API、CLI 子命令和输出格式不变。
- `recognizeDocument` 和 `hasPdfSupport` 为新增导出，不影响现有 `recognize` / `detect` / `info` 路径。
- `pdfium-native` 为 optional dependency；不安装 PDFium 时所有非 PDF 功能正常工作。
- `document` CLI 子命令不覆盖已有的 `recognize` / `detect` / `info` 子命令。
