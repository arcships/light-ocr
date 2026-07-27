# npm 0.5.4 发布记录

状态：候选修复中<br>
日期：2026-07-27

## 发布边界

- Stable：`@arcships/light-ocr@0.5.4`、runtime `0.1.4` 与六个平台 native
  `0.5.4`；Small model 继续复用不可变的 `0.3.4`。
- Preview：Tiny/Medium facade `0.1.3` 以及
  `@arcships/light-ocr-document@0.1.0` 只发布到 `next`。
- Document Preview 精确依赖 Small `0.5.4` 和 `pdfium-native@0.6.1`。

## 必须通过的门禁

- [ ] Workspace Node tests 与 TypeScript declarations
- [ ] Python release/package contract tests
- [ ] 六个平台 stable closure 离线、禁脚本安装与真实 OCR
- [ ] 六个平台 Document Preview 启用安装脚本、真实 PDF render + OCR
- [ ] npm tarball README、bin、依赖版本与 release manifest 审计
- [ ] `next` 发布后按 integrity 验证
- [ ] stable closure promotion 后逐包验证 `latest`
- [ ] GitHub `v0.5.4` Release 仅在 promotion 完成后标记 Latest

远端 workflow run、tarball integrity 和 promotion 证据在实际完成后填写；在此
之前不得将本记录标记为“已发布”。
