# npm 0.5.6 发布准备记录

状态：发布前验证完成，尚未发布。npm registry 与 dist-tag 均保持
`0.5.5` 发布后的状态。

发布身份：

- PDF fallback 实现提交：
  [`196ebde`](https://github.com/arcships/light-ocr/commit/196ebdee7c3adead047b485e6a558133bd0263c5)
- Windows release builder 修复：
  [`d80a868`](https://github.com/arcships/light-ocr/commit/d80a8681485c470b48699e24fa18661d6a60b718)
- 合并后基线：
  [Core 30534418143](https://github.com/arcships/light-ocr/actions/runs/30534418143)、
  [Native WebGPU 30534418001](https://github.com/arcships/light-ocr/actions/runs/30534418001)
- 首次诊断演练：
  [30535427156](https://github.com/arcships/light-ocr/actions/runs/30535427156)
  （macOS/Linux 通过，Windows 暴露 `npx.cmd` 寻址问题，未组装、未发布）
- 完整发布演练：
  [30535822947](https://github.com/arcships/light-ocr/actions/runs/30535822947)
  （六平台通过，`publish_to_registry=false`）

## 用户可见变化

- 修复常见非嵌入中文字体 PDF 的渲染前置缺陷。PDFium 现在从当前
  native npm 包内的 Noto Sans SC fallback 字体绘制页面，再把正确的
  像素交给 OCR。
- 字体、OFL 许可证、PDFium addon 与共享库全部随匹配平台的 npm 包
  交付。支持 `npm install --ignore-scripts`；客户机安装和运行均不下载
  字体、不编译 native addon，也不要求系统预装中文字体。
- 锁定的 `NotoSansSC-Regular.otf` 为 `8,331,336` bytes，SHA-256 为
  `faa6c9df652116dde789d351359f3d7e5d2285a2b2a1f04a2d7244df706d5ea9`。
- 没有 public API 或 schema 破坏。`@arcships/light-ocr` 的图片、PDF
  与多页图片入口保持不变。
- `0.5.5` 的不可变包继续保留，但不应再用于需要可靠处理非嵌入中文
  字体 PDF 的场景。

## 待发布版本闭包

| 成熟度 | 包 | 候选版本 | 发布后标签计划 |
| --- | --- | ---: | --- |
| stable | `@arcships/light-ocr` | `0.5.6` | `next` 验证后晋升 `latest` |
| stable | `@arcships/light-ocr-runtime` | `0.1.6` | `next` 验证后晋升 `latest` |
| stable | 六个平台 native | `0.5.6` | `next` 验证后晋升 `latest` |
| compatibility | `@arcships/light-ocr-document` | `0.1.2` | 保持 `next` |
| preview | `@arcships/light-ocr-tiny` | `0.1.5` | 保持 `next` |
| preview | `@arcships/light-ocr-medium` | `0.1.5` | 保持 `next` |

模型包不重发新版本：Small 继续使用 `0.3.4`，Tiny/Medium 继续使用
`0.1.0`。演练会重建或取得模型 tarball 来完成离线安装验证；发布器只在
registry 中的同版本 integrity 完全一致时跳过这些不可变包。

## 六平台 native 候选

下表来自完整演练的 `release-manifest.json`。增量以已发布 `0.5.5`
tarball 为基线；用户只安装当前平台对应的一个 native 包。

| 平台包 | 压缩 bytes | 相对 0.5.5 | 解包 bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| `darwin-arm64` | 22,889,542 | +7,227,406 | 56,356,978 | `ae325aaf9a3ff79cd184a862ea36687fa32227bf5a048136f02591626ce16342` |
| `darwin-x64` | 24,853,681 | +7,229,542 | 62,109,821 | `4f3d2f9cb6fba776181e327e7321787d91b516c0a39497d8ebf2ad56bf6112cc` |
| `linux-arm64-gnu` | 20,453,485 | +7,227,462 | 40,261,084 | `6dfbedc1637ff17aa13117925ca08f80b07367d62216f90a05b7614bff038078` |
| `linux-x64-gnu` | 27,033,591 | +7,226,822 | 59,390,897 | `eb67edb08db86998c8b232c42df90eda47c0b1a30311f78e1f7f5023f0c22a15` |
| `win32-arm64` | 16,663,753 | +7,232,623 | 30,985,476 | `0ad4e90cadd230405c244a309f9dbaac33b371ae9e6a4868d7eaf31c5962723d` |
| `win32-x64` | 30,387,143 | +7,232,239 | 63,070,260 | `bcbd5b92526f0a640db53154f6482ee0c58a4a9d3c559c5ecac0611f2b21a2dd` |

## 已通过的发布门

- [x] 合并后的六平台 Core、sanitizer、fuzzer 与 oracle
- [x] Linux/Windows Native WebGPU contract
- [x] 11 个新 package identity 的 npm registry 空位检查
- [x] 六个平台从锁定源码重建 PDFium addon
- [x] 六个平台 native 包包含 fallback 字体与 OFL 许可证
- [x] 六个平台在离线模式且禁用安装脚本的条件下安装完整闭包
- [x] 六个平台运行真实图片 OCR
- [x] 六个平台验证非嵌入 `STSong-Light` 映射到 `Noto Sans SC`
- [x] 六个平台验证非空 PDF raster 与端到端 `中文测试` OCR
- [x] 候选 tarball manifest、bytes、SHA-256 与 npm integrity
- [ ] 以 `publish_to_registry=true` 发布不可变候选到 `next`
- [ ] 从 npm registry 回装并核对 integrity
- [ ] 将 stable Small/runtime/native 闭包晋升到 `latest`
- [ ] 创建 `v0.5.6` GitHub Release

## 实际发布顺序

1. 从 `main` 重新运行 `npm release`，版本 `0.5.6`，
   `publish_to_registry=true`；流水线先重复相同构建与 smoke，再只向
   `next` 写入候选。
2. 核对 registry integrity 与完整闭包回装结果。
3. 运行 `npm promote`，只把 Small `0.5.6`、runtime `0.1.6` 与六个
   native `0.5.6` 晋升到 `latest`。
4. 创建 `v0.5.6` tag/GitHub Release，并把本记录状态和最终 run ID
   更新为已发布。

Tiny、Medium 与 Document 不属于 stable promotion；它们继续停留在
`next`。整个发布不依赖外部用户验证或采用证明，所有阻断门均由源码、
候选 tarball、真实六平台 runner 和 npm registry identity 自证完成。

## GitHub Release 文案草案

Release name：

`墨字归真，六境同明 · Restore Chinese PDF rendering in 0.5.6`

Highlights：

- Fix PDF rasterization when documents reference common non-embedded Chinese
  fonts.
- Bundle a checksum-pinned Noto Sans SC fallback font and its OFL license in
  every supported native package.
- Keep installation and runtime fully self-contained: no postinstall download,
  system-font requirement, GitHub access, or local compilation.
- Preserve the existing image, PDF, and multi-page APIs without schema changes.
- Add about 7.23 MB compressed and 8.34 MB unpacked to the one native package
  selected for the customer's platform.

Versions：

- `@arcships/light-ocr@0.5.6`
- `@arcships/light-ocr-runtime@0.1.6`
- six native platform packages at `0.5.6`
- Document compatibility facade `0.1.2` under `next`
- Tiny/Medium preview facades `0.1.5` under `next`
- unchanged Small `0.3.4` and Tiny/Medium `0.1.0` model packages

Verification：

- pre-release dry-run:
  <https://github.com/arcships/light-ocr/actions/runs/30535822947>
- release workflow: 待实际发布后补入
- stable promotion: 待晋升后补入
