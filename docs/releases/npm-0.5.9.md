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

- 主包保持原来的约 39 MB Small 模型承诺：模型包 `0.3.4` 及其锁定制品不变，不加入 AMD/OpenVINO 运行库或 AMD 编译模型。
- 默认 runtime 的八个平台依赖均保持基础运行时，Auto 沿用 Apple/WebGPU/CPU 路线。
- 两套 NPU 独立分发：`@arcships/light-ocr-openvino-linux-x64-gnu@0.5.9`、`@arcships/light-ocr-amdnpu-linux-x64-gnu@0.5.9`。用户单独安装后，配置 `execution.provider` 为 `openvino` 或 `amdnpu`。optional peer 不会自动安装；安装后也不进入默认 Auto。
- 支持包各自包含匹配的 addon、原生依赖、哈希、许可证与 SBOM；AMD 包另外含 Small 0.3.4 的 20 个真实 BF16 context，体积较大。系统仍需厂商驱动，无需终端用户安装完整 SDK/Python。
- 不要求 NPU 真机验收，`deviceValidated: false`；Windows、arm64、musl 不含这两套支持包。

## Release 操作

1. 合并拆包修正，确认 main 中 Core 为 `0.5.9`。
2. 运行 `npm release`，输入 `version=0.5.9`、`publish_to_registry=false`，生成普通八平台候选包。基础打包禁止 NPU provider 或目录混入。
3. 独立运行 `NPU support packages`（`npu-support-release.yml`），输入 `version=0.5.9`、`publish_to_registry=false`，生成两套单独安装的支持包。
4. 记录最终 main 来源提交、两个构建 run、npm tarball 的精确大小/SHA-256；审阅基础安装闭包，不把支持包归档总量视为主包大小。
5. 按既有正式流程发布基础包到 `next`，完成 registry 回装后晋升 stable Small/runtime/native。独立支持包通过其自身 workflow 发布到 `next`，Document/preview 继续保留 `next`。
6. 将草稿 target 指向最终 main 提交，补录实际发布身份、日期和正式制品链接，再公开 `v0.5.9`。

## 当前证据与废弃候选

- 实现 PR #67、版本准备 PR #68 已合并；此次拆包修正仍待合并。
- 旧候选 run `37480252872` 八平台构建及 smoke 虽通过，但 Linux x64 误含两套 NPU，单包 `577,384,672` bytes、解压 `1,455,176,275` bytes。**该候选不用于正式发布，须从拆包后的 main 重建。**
- 双 NPU 源码构建 run `37469626258` 通过；AMD 官方 SDK 已取得，20 桶实际编译，无 AMD 设备推理证据。
- 原始 AMD SDK 身份见 `tools/npu/amdnpu-source.lock.json`；部署输入见 `tools/npu/amdnpu.lock.json`。约 508 MiB 的部署归档仍作为独立 AMD 支持包的构建输入，不进入普通包。
- 13 个基础新 package identity 的空位检查已通过；两个新 NPU 支持包须独立发布。最终拆包候选的 tarball 和 run 在生成后补录。

## 本地拆包构建证据

三个 addon（基础 CPU、独立 OpenVINO、独立 AMD）均已分别构建并完成 staging。基础库存仅含 CPU provider，无 NPU 目录，native 文件共 `33,091,890` bytes（此处不含正式 WebGPU/PDF 分发附件）。两个独立支持包已实际生成 tarball：

| Package | Compressed bytes | Unpacked bytes |
| --- | ---: | ---: |
| `@arcships/light-ocr-amdnpu-linux-x64-gnu` | 548,609,874 | 1,390,267,314 |
| `@arcships/light-ocr-openvino-linux-x64-gnu` | 26,363,501 | 72,478,453 |

这是本地拆包证据，不是新的八平台正式候选结果。未运行新的本地测试或真机验收。

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
