# npm 0.5.5 发布记录

状态：候选实现与发布门已配置，等待六平台远端构建、npm publication 和
stable promotion。

## 目标

- `@arcships/light-ocr@0.5.5` 直接支持图片、PDF 和多页图片。
- runtime `0.1.5` 精确选择六个平台 native `0.5.5`。
- 每个平台 native 包内置 `pdfium-native@0.6.1` 的 addon 与 PDFium
  共享库；客户机不运行安装脚本、不访问 GitHub、不本地编译。
- `@arcships/light-ocr-document@0.1.1` 仅作旧 import/命令兼容层。

## 必须通过的发布门

- [ ] 六个平台 native 包包含 `pdfium.node` 与匹配的共享库
- [ ] 六个平台使用 `npm install --offline --ignore-scripts` 安装完整 stable closure
- [ ] 六个平台从主包运行真实 PDF render + OCR
- [ ] Linux registry 验证在禁用网络命名空间内同时通过图片与 PDF OCR
- [ ] npm registry integrity 验证
- [ ] stable promotion
- [ ] GitHub `v0.5.5` Release
