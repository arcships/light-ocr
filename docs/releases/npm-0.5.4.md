# npm 0.5.4 发布记录

状态：已发布并完成 stable promotion<br>
日期：2026-07-27

- Release source/tag：`7206ba18fbc42921d4a728c7ff123131d3f9f8b0` / `v0.5.4`
- 六平台 dry-run：[30236452750](https://github.com/arcships/light-ocr/actions/runs/30236452750)
- npm publication：[30237035832](https://github.com/arcships/light-ocr/actions/runs/30237035832)
- stable promotion：[30237918764](https://github.com/arcships/light-ocr/actions/runs/30237918764)

## 发布边界

- Stable：`@arcships/light-ocr@0.5.4`、runtime `0.1.4` 与六个平台 native
  `0.5.4`；Small model 继续复用不可变的 `0.3.4`。
- Preview：Tiny/Medium facade `0.1.3` 以及
  `@arcships/light-ocr-document@0.1.0` 只发布到 `next`。
- Document Preview 精确依赖 Small `0.5.4` 和 `pdfium-native@0.6.1`。

## 必须通过的门禁

- [x] Workspace Node tests 与 TypeScript declarations
- [x] Python release/package contract tests
- [x] 六个平台 stable closure 离线、禁脚本安装与真实 OCR
- [x] 六个平台 Document Preview 启用安装脚本、真实 PDF render + OCR
- [x] npm tarball README、bin、依赖版本与 release manifest 审计
- [x] `next` 发布后按 integrity 验证
- [x] stable closure promotion 后逐包验证 `latest`
- [x] GitHub `v0.5.4` Release 仅在 promotion 完成后标记 Latest

promotion 只推进六个 native `0.5.4`、runtime `0.1.4` 和 Small facade
`0.5.4`。Tiny/Medium/Document 没有进入 promotion。npm 为首次发布的
Document 包名自动保留 `latest=0.1.0`，但它仍是显式安装的 Preview，文档只
推荐 `@next`。
