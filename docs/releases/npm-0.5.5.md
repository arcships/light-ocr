# npm 0.5.5 发布记录

状态：已发布。stable npm 闭包、六平台离线安装与真实 PDF OCR 烟测、
registry integrity、稳定晋升和 GitHub Release 均已完成。

发布身份：

- 实现提交：[`0f5562e`](https://github.com/arcships/light-ocr/commit/0f5562ea80308860f095c195db4443dd0e20e215)
- npm 发布流水线：[30239210355](https://github.com/arcships/light-ocr/actions/runs/30239210355)
- stable 晋升流水线：[30239834429](https://github.com/arcships/light-ocr/actions/runs/30239834429)
- GitHub Release：[`v0.5.5`](https://github.com/arcships/light-ocr/releases/tag/v0.5.5)

## 目标

- `@arcships/light-ocr@0.5.5` 直接支持图片、PDF 和多页图片。
- runtime `0.1.5` 精确选择六个平台 native `0.5.5`。
- 每个平台 native 包内置 `pdfium-native@0.6.1` 的 addon 与 PDFium
  共享库；客户机不运行安装脚本、不访问 GitHub、不本地编译。
- `@arcships/light-ocr-document@0.1.1` 仅作旧 import/命令兼容层。
- Tiny/Medium facade `0.1.4` 同步精确依赖 runtime `0.1.5`，避免组合安装
  解析旧 runtime。

## 必须通过的发布门

- [x] 六个平台 native 包包含 `pdfium.node` 与匹配的共享库
- [x] 六个平台使用 `npm install --offline --ignore-scripts` 安装完整 stable closure
- [x] 六个平台从主包运行真实 PDF render + OCR
- [x] Linux registry 验证在禁用网络命名空间内同时通过图片与 PDF OCR
- [x] npm registry integrity 验证
- [x] stable promotion
- [x] GitHub `v0.5.5` Release

## 最终 npm 标签

- `@arcships/light-ocr@0.5.5`：`latest` / `next`
- `@arcships/light-ocr-runtime@0.1.5`：`latest` / `next`
- 六个平台 native `0.5.5`：`latest` / `next`
- `@arcships/light-ocr-tiny@0.1.4`、`@arcships/light-ocr-medium@0.1.4`：
  保持 `next`，不进入 stable closure
- `@arcships/light-ocr-document@0.1.1`：保持 `next`，仅作兼容入口
