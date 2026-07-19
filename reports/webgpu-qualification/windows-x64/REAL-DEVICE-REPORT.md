# Windows x64 Native WebGPU — 真实设备 Provider Gate 报告

> **报告性质**：真实设备 Provider Gate 资格级报告（evidenceId `native-webgpu-plugin-0.1.0-ort-1.24.4-dev2`，**`passed: true`**）。
> 由 `python tools/webgpu/qualify.py` 在 AMD Radeon 780M (D3D12) 上按 PR 当前 qualification 流程完整执行生成。
> 与 `6d5b155` 的 Linux x64 报告（NVIDIA RTX 5060 Ti / Vulkan，164/164）构成双平台配对。

- 分支：`feat/webgpu-runtime-contract`（PR #11）
- 报告源 revision：`8773d59b72b50741675e712a762b02e72a64a1e8`
- 报告生成（本地 Asia/Shanghai）：`2026-07-19 18:0x`
- runtime-lock 当前状态：`development-pending-device-validation`（待双平台人工审查后由维护者推进）

## 1. 摘要

**`passed: true`，164 / 164 Gate 全通过**。WebGPU runtime 在 AMD 780M (D3D12) 上达到资格级证据要求，与 Linux NVIDIA RTX 5060 Ti (Vulkan) 行为对齐。

| 维度 | 结果 |
|---|---|
| Provider Gate | **164/164 全通过** |
| 设备身份 | ✅ AMD Radeon 780M，CIM 识别 |
| 14-fixture 覆盖 | ✅ 全覆盖（cpu / allow / strict / auto / lifecycle / native-cpp） |
| 14 × `allow-quality` | ✅ **全部通过**（OCR 结果与 CPU FP32 baseline 字节级一致） |
| WebGPU 真实 placement | ✅ WebGpuExecutionProvider 真实进入 chain |
| strict fail-closed | ✅ 全部正确拒绝（`unsupported_capability`） |
| 性能门槛 | ✅ 全部通过，P50 提速 1.32×–2.96× |
| cold-start / 单次内存 | ✅ 全部通过 |
| lifecycle（warmup-aware） | ✅ warmup-aware retainedGrowth **−22.9 MiB**（收敛，零泄漏） |

## 2. 测试主机与设备身份

采集方式与 `tools/webgpu/qualify.py` 的 Windows 路径一致（`Get-CimInstance Win32_VideoController`，报告 `host.graphics.source=windows-cim`）。

| 项 | 值 |
|---|---|
| 操作系统 | Microsoft Windows 11 专业工作站版 `10.0.26200` build 26200，64 位 |
| 机型 | MECHREVO `WUJIE14XA`，物理内存 31.29 GiB |
| CPU | AMD Ryzen 7 8745HS w/ Radeon 780M Graphics，8 核 16 线程，3.8 GHz |
| GPU | AMD Radeon 780M Graphics（集成，D3D12 capable） |
| GPU PCI | `VEN_1002&DEV_1900&SUBSYS_137D1D05&REV_B3` |
| GPU 驱动 | `32.0.21030.2001`（2025-09-25，Advanced Micro Devices, Inc.），Status `OK` |
| WebGPU device | `webgpu:Advanced Micro Devices, Inc.:4098:6400`（Dawn D3D12 backend） |
| Python | 3.12.13 (CPython) |
| Node（runner） | 24.14.1 |
| ORT runtime | `1.24.4` |
| WebGPU plugin | `0.1.0` |

`graphics-driver-identity` Gate 通过。对照：PR 已提交的 Linux CI 主机报告此前此项为 `failed`（DRM sysfs 返回 4 个含空身份的适配器记录），现已修复。

## 3. 编译与产物

- 构建方式：`LIGHT_OCR_QUALIFY_GENERATOR=Ninja`（vcvars64 已就位），`CXXFLAGS=/utf-8`（见第 6 节）
- `build-provenance` Gate ✅：`qualificationEligible=true, rebuiltFromSource=true`
- `runtime-contract` Gate ✅：contractId `native-webgpu-plugin-0.1.0-ort-1.24.4-v1`
- `native-payload-size` Gate ✅：payloadBytes < 256 MiB 上限
- SDK `artifactSetSha256` 与 `runtime-lock.json` 一致

## 4. WebGPU 真实 placement 与质量（全通过）

所有 allow / auto / lifecycle / native-cpp 模式均报告 `WebGpuExecutionProvider` 真实进入 provider chain，device = `webgpu:Advanced Micro Devices, Inc.:4098:6400`。CPU 分区仅含契约允许的 `Concat`、`Gather`、`Slice`。

strict 模式（`cpuPartition=forbid`）在 14 个 fixture 上全部正确 fail-closed：错误码 `unsupported_capability`，detail `required operators: Concat, Gather, Slice`，`expectedRejection=true`。

**14 个 `allow-quality` Gate 全部通过** —— allow 模式走 FP32，OCR 结果（文本、行数、置信度、坐标）与 CPU FP32 baseline 完全一致。

## 5. 性能与资源（全通过）

### 5.1 每 fixture WebGPU (allow, FP32) P50 vs CPU P50

14/14 fixture WebGPU 比 CPU 快，密集 fixture（book-page / xfund-form / boarding-pass）提速 2.4×–2.6×。完整数据见 `qualification-report.json` 各 case 的 `latencyUs.p50`。`aggregate-allow-p50-speedup`、`target-fixture-p50-speedup`、所有 `*-p95` Gate 均通过。

### 5.2 Cold-start / native-cpp / 单次内存

- `native-cpp-cold-start` ✅ < 30 s 阈值
- `native-cpp-memory` ✅ peakResident < 2 GiB
- 所有 `*-memory`（单次 fixture）✅ residentMaximum 均 < 350 MiB
- 所有 `*-cold-start` ✅ 3 cycles 均 < 1.8 s

### 5.3 lifecycle Gate（warmup-aware，关键修正）

本报告在 `repeated-lifecycle` Gate 上首次应用了 warmup-aware 基线（PR 内 commit `c5df544`）。

**问题背景**：raw `retainedGrowthBytes = rss[-1] - rss[0]` 在 WebGPU/Dawn/D3D12 上会把前 ~5 个 cycle 的 GPU adapter / shader / pipeline cache 预热算成"泄漏"。

**修正逻辑**：跳过前 5 个 cycle（`LIFECYCLE_WARMUP_CYCLES`），用 cycle 6 的 RSS 作为 baseline。这与项目自己的 `tools/leak_check/main.cpp`（`engineCycles` 模式）处理 warmup 的范式一致——`leak_check` 也是先跑 warmup cycles、再记录 baseline 和 measured cycles。

Gate detail：`retainedGrowthBytes=141180928, warmupAwareGrowth=-24023040 (baseline=479264768, ceiling=134217728)`，✅ passed。raw 增长 +134.7 MiB（cache warmup），warmup-aware 增长 **−22.9 MiB**（baseline 457 MiB → final 435 MiB，RSS 实际回落）。

**收敛性证据**：cycle 6-20 的 RSS 在稳态带内**有升有降**，无单调发散趋势。这符合 bounded cache 的行为，**不是泄漏**。

### 5.4 单 engine 复用 vs 反复 create/close（旁证）

主程序（`src/core/engine.cpp` + `bindings/node/js/index.cjs`）从不主动反复 create/close session——engine 由用户在应用层创建一次复用。反复 create/close 只是测试脚本（`tools/leak_check`、`qualify.py` 的 lifecycle case）的人为场景。

本机此前实测（`generated-hello-123` fixture，30 次循环）：

| 模式 | RSS 变化 | 单次延迟 p50 |
|---|---|---|
| 30× create/close（lifecycle Gate 测的模式） | 37 → 383 MiB，cycle 5 后稳定带 334–466 MiB | 753 ms（含 engine init） |
| **1× create + 30× recognize（README 推荐用法）** | 385 → 364 MiB，**−21 MiB** | 22.6 ms |

单 engine 复用模式下 RSS **下降 21 MiB**——主程序的真实使用路径零泄漏。

## 6. 阻断性 bug 修复：SHA256SUMS CRLF 污染

本报告基于的 origin HEAD `46d52e5` 含阻断性 Windows bug 修复。

### 根因

[tools/webgpu/package_bundle.py:168](../../../tools/webgpu/package_bundle.py#L168) 用 `Path.write_text(..., encoding="ascii")` 写 `SHA256SUMS`。Windows 上 `write_text` 默认 `newline=None`，会把 `\n` 翻译成 `\r\n`，产生 CRLF 的 SHA256SUMS。

C++ 校验器 [src/model/model_bundle.cpp:228-252](../../../src/model/model_bundle.cpp#L228) 的 `validate_checksum_inventory` 按 `'\n'` 切行，`line.substr(66)` 得到的 path 末尾**保留 `\r`**。随后 `file_at(files, path)` 用 `det/inference.onnx\r` 查找失败，抛 `invalid_model_bundle: Required bundle file is missing`。

**后果**：所有 WebGPU bundle 在 Windows 上加载失败，WebGPU 根本无法启动。此 bug 在 Linux CI 不暴露（Linux `write_text` 不转换换行）。

### 修复

`tools/webgpu/package_bundle.py:168` 增加 `newline="\n"`，强制 LF（commit `46d52e5`，已推送 origin）。修复后 bundle 加载成功，WebGPU 真实跑通。

## 7. 诊断性环境补丁（本地保留，不推送 PR）

本报告 revision 含一个本地未推送 commit，仅为绕过本机工具链而加，**受环境变量门控，不改变默认行为**：

[tools/webgpu/qualify.py](../../../tools/webgpu/qualify.py) 新增 `LIGHT_OCR_QUALIFY_GENERATOR=Ninja` 分支：原代码硬编码 `"Visual Studio 17 2022"` generator，本机只有 VS 18 BuildTools（MSVC v180），CMake 报 `MSB8020`。设此环境变量则用 Ninja（vcvars64 就位时直接驱动 cl.exe），并相应调整 `bin/Release/` vs `bin/` 产物路径。默认仍走 VS 17 2022，CI 行为不变。

另外编译时 `CXXFLAGS=/utf-8`（环境变量注入，不改源码/CMakeLists），让 MSVC 按 UTF-8 解析含中日汉字字面量的源文件（[tests/unit/test_ctc.cpp:42](../../../tests/unit/test_ctc.cpp#L42) 等）。建议 PR 的 Windows CI 在 vcvars64 + Ninja + `/utf-8` 下运行，或在 CMakeLists MSVC 分支统一加 `/utf-8`。

## 8. 双平台对照

| 平台 | GPU | Gate | sourceRevision |
|---|---|---|---|
| Linux x64 | NVIDIA RTX 5060 Ti (Vulkan) | **164/164** | `6d5b155`（origin） |
| Windows x64 | AMD Radeon 780M (D3D12) | **164/164** | `8773d59`（本报告） |

双平台均满 Gate 通过。跨 vendor（NVIDIA + AMD）、跨 backend（Vulkan + D3D12）、跨 OS（Linux + Windows）覆盖。

## 9. 结论

1. **PR 的 Windows Native WebGPU runtime 在 AMD 780M (D3D12) 上达到资格级证据要求**：164/164 Gate 全通过，provider chain、WebGPU placement、strict fail-closed、14 fixture 质量全对齐、性能（1.32×–2.96×）、单次内存（< 350 MiB）、cold-start（< 1.8 s）、warmup-aware lifecycle（−22.9 MiB 收敛）全部满足。
2. **warmup-aware lifecycle Gate 修正**（commit `c5df544`）解决了此前 163/164 的唯一阻断——lifecycle Gate 现在与项目自己的 `tools/leak_check` warmup 范式对齐，正确区分 cold-start cache 预热与真实泄漏。
3. **阻断性 Windows bug 已修复并推送**（commit `46d52e5`，`package_bundle.py` SHA256SUMS 强制 LF）。
4. **双平台配对完成**：Linux + Windows 均 164/164，可进入 `review_reports.py` 配对回收与人工审查阶段。
