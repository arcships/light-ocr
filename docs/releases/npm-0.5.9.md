# npm 0.5.9 发布准备

[English version](npm-0.5.9.en.md)

状态：准备中，尚未发布 npm、尚未创建公开 tag。GitHub Release 使用草稿；制品大小、SHA-256、发布 run 和晋升结果在实际生成后补录，不沿用上一版本数据。

## 发布身份与版本

- NPU 实现：[PR #67](https://github.com/arcships/light-ocr/pull/67)，已合并。
- 发布准备：[PR #68](https://github.com/arcships/light-ocr/pull/68)；`v0.5.9` GitHub Release 草稿已创建，尚未公开。
- 版本采用补丁递增：Core/Small/native `0.5.8 → 0.5.9`。
- 本版本的正式源码应指向版本准备 PR 合并后的 main 提交，不能绑定旧的 0.5.8 提交。

| 包 | 准备版本 | 发布标签计划 |
| --- | --- | --- |
| Small `@arcships/light-ocr` | `0.5.9` | 先 `next`，再 `latest` |
| model-free runtime | `0.1.9` | 先 `next`，再 `latest` |
| 八个 native 平台包 | `0.5.9` | 先 `next`，再 `latest` |
| Document compatibility facade | `0.1.5` | `next` |
| Tiny/Medium preview facade | `0.1.8` | `next` |
| Small model | `0.3.4` | 复用已有不可变制品 |
| Tiny/Medium model | `0.1.0` | 复用已有不可变制品 |

## 用户可见变化

- Linux x64 glibc 平台包默认附带锁定的 OpenVINO 2026.4.0 原生运行时；Intel NPU 在 Auto 中位于 WebGPU/CPU 前。有可用 NPU 时运行检测和 20 桶识别；无设备、模型不支持或驱动过旧时按创建阶段策略继续选择后续 provider。
- Linux x64 glibc 发布构建默认附带 Ryzen AI 1.8 原生部署库及 Small 0.3.4 的 20 个真实 BF16 识别 context。AMD 始终默认关闭，不进入 Auto；通过 `execution: { provider: "amdnpu" }` 或 `--provider amdnpu` 显式开启。检测使用 CPU，目标为 STX/KRK；Tiny/Medium 需另行编译，不能复用 Small context。
- 发布工作流自动下载哈希锁定的 AMD 部署归档，不需要维护者另行提供 SDK。终端用户仍需系统厂商驱动，无需 SDK/Python 安装。
- NPU 真机验收不作为发布前置；未验证的 NPU 会话保持 `deviceValidated: false`。库存、哈希、模型绑定、许可证和基础运行时的既有发布检查保留。
- 包内 NPU 描述采用 schema 2.1；不含 NPU 的平台保持 2.0。新增模型配置、版本与产物信息，Node loader 在装载前完成验证。
- 其他平台继续使用现有 Apple/WebGPU/CPU 路线；Windows、arm64 和 musl 没有本次 NPU 后端。模型包版本、Node 安装下限 `>=22.0.0` 及 public API 保持既有契约。

## Release 操作

1. 合并版本准备 PR，确认 main 中 Core 为 `0.5.9`。
2. 运行 `npm release` workflow，输入 `version=0.5.9`、`publish_to_registry=false`。Linux x64 默认从锁文件取得 Intel 与 AMD SDK，无需填写 NPU run ID。
3. 可选 `npu_sdk_run_id` 仅用于覆盖默认 SDK 输入；须同时包含 `openvino/` 与 `amdnpu/`。草稿附件下载使用 workflow 的 GitHub token；保持 Release 草稿及附件可用。
4. 审阅八平台构建、离线安装/图片/PDF smoke 与 tarball manifest；将运行 ID、精确来源提交、13 个新 package identity 和制品哈希补录本文件。
5. 用 `publish_to_registry=true` 发布到 `next`，记录发布 run，并确认 registry 回装结果。
6. 运行 `npm promote`：`version=0.5.9`、`release_run_id=<正式发布run>`、`tag=latest`。仅晋升 stable Small/runtime/native；Document 与 Tiny/Medium 保留 `next`。
7. 将 GitHub Release 草稿 target 更新为正式 main 来源提交，附上 release-manifest 和公开制品链接；确认发布结果后更新 Changelog 日期、两份发布记录和 Release 状态，再发布 `v0.5.9`。

准备阶段不会主动调用 registry publish、晋升 dist-tag 或公开发布 GitHub Release。

## 当前证据

- PR #67 的 Linux native 与 workspace CI 均通过；这属于实现来源，不是 0.5.9 八平台发布结果。
- 13 个新 package identity 的 npm 空位检查通过；加上复用的两个 preview 模型，release manifest 仍包含 15 包。
- Intel+AMD Core/Node addon 已按 `LIGHT_OCR_VERSION=0.5.9` 重新配置并构建通过；版本闭包、lockfile 和脚本语法检查通过，未运行新的本地测试。实际 tarball、发布/晋升 run 和 registry integrity 尚未生成。
- AMD 20 桶已实际编译，20 个原生库与 56 个库存文件已导入；原始 SDK、打包后运行库及模型哈希均已记录。本次没有执行 AMD 推理，不安排 NPU 真机验收。

## 回滚

已发布 npm 版本不可覆盖。若 0.5.9 晋升后需要回滚，使用 0.5.8 的已归档正式发布 artifact（run `36013862673`）恢复 stable Small/native `0.5.8` 与 runtime `0.1.8` 的 `latest` 标签。Document 与 preview 标签单独处理；不删除用户已安装的 0.5.9 产物。

## AMD 部署制品

- 归档：`amdnpu-1.8.0-linux-x64-ppocrv6-small-0.3.4-opt-in-v1.tar.gz`
- 大小：`532,631,585` bytes（约 508 MiB）；SDK 库存共 `1,356,635,270` bytes（约 1.26 GiB），Linux x64 npm 包因此显著增大。
- 归档 SHA-256：`bee70b00f3254943751b9460628295696d9901f22887f11b0c791191847fb016`
- Artifact set SHA-256：`5df515842656974be5517ae40b408427f0ffdbb63473c1ced1b610f2a0afc8a4`
- Small 识别源 SHA-256：`5435fd747c9e0efe15a96d0b378d5bd157e9492ed8fd80edf08f30d02fa24634`
- 官方 SDK 原始归档身份见 `tools/npu/amdnpu-source.lock.json`，最终部署归档身份见 `tools/npu/amdnpu.lock.json`。归档已附于 GitHub 草稿，正式 npm tarball 身份须由发布工作流另行生成。

## GitHub Release

Tag：`v0.5.9`（草稿阶段未公开创建）。

Release name：`双芯入卷，轻舟再发 · Ship Intel NPU and opt-in AMD support in 0.5.9`
