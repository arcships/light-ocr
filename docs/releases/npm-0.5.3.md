# npm 0.5.3 发布记录

状态：已发布
日期：2026-07-26

## 发布身份

- 版本：`0.5.3`
- 变更类型：Minor（新功能）
- 关联：N3 文档入口（roadmap §8）

## 范围

`0.5.3` 完成 N3 文档入口功能并发布到 npm。Small facade 新增 `recognizeDocument()` API
和 `light-ocr document` CLI 子命令，支持 PDF 和多页图片的统一文档 OCR。PDF
渲染通过 `pdfium-native`（lazy-loaded、optional）实现；当 PDFium 不可用时
`hasPdfSupport()` 返回 `false`，API 以明确的 `unsupported_capability` 错误
降级，不影响已有的单图 `recognize` 路径。

| 角色 | package/version | 变化 | channel |
| --- | --- | --- | --- |
| Stable facade | `@arcships/light-ocr@0.5.3` | + `recognizeDocument` / `hasPdfSupport` / `document` CLI / `doctor` CLI | `next` → `latest` |
| Shared runtime | `@arcships/light-ocr-runtime@0.1.3` | + `loadNative` 导出 | `next` → `latest` |
| Native runtime | 六个 platform packages `0.5.3` | 无变化 | `next` → `latest` |
| Small model | `@arcships/light-ocr-model-ppocrv6-small@0.3.4` | 无变化 | 不变 |
| Tiny facade | `@arcships/light-ocr-tiny@0.1.2` | 无功能变化 | `next` |
| Medium facade | `@arcships/light-ocr-medium@0.1.2` | 无功能变化 | `next` |

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

### `light-ocr document` CLI

```bash
# 处理 PDF
light-ocr document report.pdf --format json

# 指定页码范围，流式输出
light-ocr document report.pdf --pages 1-10 --format jsonl

# 多张图片
light-ocr document page1.png page2.png --format text

# 指定 DPI
light-ocr document report.pdf --dpi 300 --format json
```

### `light-ocr doctor` CLI

```bash
light-ocr doctor --json
```

收集系统信息（Node.js 版本、OS、CPU、内存、native runtime 状态、可用 provider）。
不收集用户内容；hostname 使用 SHA-256 哈希。

### PDF 资源限制

- `maxPages`：默认 100
- `maxPagePixels`：默认 16,777,216（4096×4096）
- `maxTotalPixels`：默认 100,000,000
- `maxFileBytes`：默认 100,000,000
- 超限返回 `resource_limit_exceeded` 错误

## 版本历史

- `0.5.0`：N3 功能实现
- `0.5.1`：修复 native package integrity 冲突
- `0.5.2`：修复 runtime/tiny/medium integrity 冲突
- `0.5.3`：修复 package.json 编码问题和 workflow 版本引用

## 关联

- D108: PDF renderer selection (pdfium-native)
- N3: 图片与 PDF 文档入口
- Perf-0: 硬件覆盖审计
- Perf-1: CPU 性能 baseline
