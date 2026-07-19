# Windows x64 Native WebGPU — 真实设备 Provider Gate 报告

> **报告性质**：真实设备 Provider Gate 诊断级报告（evidenceId `native-webgpu-plugin-0.1.0-ort-1.24.4-dev2`，`passed: false`）。
> 由 `python tools/webgpu/qualify.py` 在 AMD Radeon 780M (D3D12) 上按 PR 当前 qualification 流程（cpu/allow/strict/auto/lifecycle，无 FP16）完整执行生成。
> 报告源 revision `6970384` = origin HEAD `46d52e5`（含 CRLF 修复）+ 本地诊断 patch（见第 6 节）。不更改 runtime-lock。

- 分支：`feat/webgpu-runtime-contract`（PR #11）
- 报告源 revision：`6970384c4ab92191baff6bd937e83d6d5a1102d7`
- 报告生成（本地 Asia/Shanghai）：`2026-07-19 15:xx`
- runtime-lock 当前状态：`development-pending-device-validation`（与本报告结论一致）

## 1. 摘要

`passed: false`，**163 / 164 Gate 通过，1 项 failed**。唯一失败是 `repeated-lifecycle`：20 次 engine create/close 循环后进程 RSS 净增长 172 MiB，超过 128 MiB 上限——疑似 WebGPU/Dawn 在 Windows D3D12 反复创建 session 时存在资源未完全释放。所有 provider chain、placement、strict fail-closed、质量、性能、cold-start Gate 均通过。

| 维度 | 结果 |
|---|---|
| Provider Gate | 163 passed / 1 failed |
| 失败项 | `repeated-lifecycle`（RSS 持续爬升，retainedGrowth 172 MiB > 128 MiB 上限） |
| 设备身份 Gate | ✅ 通过（Linux CI 主机此项 failed） |
| 14-fixture 覆盖 | ✅ 全覆盖（cpu / allow / strict，canary 含 auto / lifecycle） |
| 14 × `allow-quality` | ✅ **全部通过**（FP16 移除后 allow 走 FP32，OCR 结果与 CPU baseline 一致） |
| WebGPU 真实 placement | ✅ WebGpuExecutionProvider 真实进入 chain |
| strict fail-closed | ✅ 全部正确拒绝 |
| 性能门槛 | ✅ 全部通过，P50 提速 1.21×–2.98× |

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

`graphics-driver-identity` Gate 通过（单适配器，`Name`/`DriverVersion` 均非空）。对照：PR 已提交的 Linux CI 主机报告此项为 `failed`，因 DRM sysfs 返回 4 个含空身份的适配器记录。

## 3. 编译与产物

- 构建方式：`LIGHT_OCR_QUALIFY_GENERATOR=Ninja`（vcvars64 已就位），`CXXFLAGS=/utf-8`（见第 6 节）
- `build-provenance` Gate ✅：`qualificationEligible=true, rebuiltFromSource=true`
- `runtime-contract` Gate ✅：contractId `native-webgpu-plugin-0.1.0-ort-1.24.4-v1`
- `native-payload-size` Gate ✅：46,254,274 bytes（43.4 MiB），ceiling 256 MiB
- SDK `artifactSetSha256` `6fa272f5...cc128` 与 [runtime-lock.json](../../../tools/webgpu/runtime-lock.json) 一致

## 4. WebGPU 真实 placement 与质量（全通过）

所有 allow / auto / lifecycle / native-cpp 模式均报告 `WebGpuExecutionProvider` 真实进入 provider chain，device = `webgpu:Advanced Micro Devices, Inc.:4098:6400`。CPU 分区仅含契约允许的 `Concat`、`Gather`、`Slice`。

strict 模式（`cpuPartition=forbid`）在 14 个 fixture 上全部正确 fail-closed：错误码 `unsupported_capability`，detail `required operators: Concat, Gather, Slice`，`expectedRejection=true`。

**14 个 `allow-quality` Gate 全部通过** —— 本次使用 PR 当前流程（commit `17d6efd` 已在非 Mac 路径移除 FP16），allow 模式走 FP32，OCR 结果（文本、行数、置信度、坐标）与 CPU FP32 baseline 完全一致。对照：本机上一轮基于 FP16 路径的测试有 5 个 allow-quality failed（FP16 精度漂移），移除 FP16 后全部消失。

## 5. 性能与资源

### 5.1 每 fixture WebGPU (allow, FP32) P50 vs CPU P50

| Fixture | CPU P50 (ms) | WebGPU P50 (ms) | 提速 |
|---|---:|---:|---:|
| generated-hello-123 | 56.5 | 27.0 | 2.09× |
| generated-blank | 12.0 | 9.9 | 1.21× |
| generated-japanese-horizontal | 76.9 | 34.2 | 2.25× |
| generated-japanese-rotated | 74.3 | 33.6 | 2.21× |
| generated-traditional-horizontal | 78.9 | 36.8 | 2.14× |
| generated-low-contrast-perspective | 85.3 | 35.2 | 2.43× |
| paddleocr-boarding-pass | 862.8 | 360.9 | 2.39× |
| paddleocr-book-page | 1352.7 | 530.8 | 2.55× |
| paddleocr-captcha-handwriting | 209.6 | 97.7 | 2.15× |
| paddleocr-display-simplified | 289.7 | 97.2 | 2.98× |
| paddleocr-garden-sign | 42.6 | 24.0 | 1.78× |
| paddleocr-rec-phone | 26.5 | 18.6 | 1.43× |
| paddleocr-rec-simplified | 25.0 | 18.0 | 1.39× |
| paddleocr-xfund-form | 3284.1 | 1347.1 | 2.44× |

**14/14 fixture WebGPU 比 CPU 快**，密集 fixture（book-page / xfund-form / boarding-pass）提速 2.4×–2.6×。`aggregate-allow-p50-speedup`、`target-fixture-p50-speedup`、所有 `*-p95` Gate 均通过。

### 5.2 Cold-start / native-cpp / 单次内存

| Gate | 结果 | detail |
|---|---|---|
| `generated-hello-123:cpu-cold-start` | ✅ | 3 cycles 均 < 1.4 s |
| `generated-hello-123:allow-cold-start` | ✅ | 3 cycles 均 < 1.8 s |
| `generated-hello-123:auto-cold-start` | ✅ | 3 cycles 均 < 1.8 s |
| `native-cpp-cold-start` | ✅ | `coldStartUs=1282015`（< 30 s 阈值） |
| `native-cpp-memory` | ✅ | peakResident `252.7 MiB`（< 2 GiB） |
| 所有 `*-memory`（单次 fixture） | ✅ | residentMaximum 均 < 350 MiB |
| `native-cpp:auto` | ✅ | provider chain `[WebGpuExecutionProvider, CPUExecutionProvider]`，inference median 27.2 ms |

### 5.3 ❌ `repeated-lifecycle`（唯一 failed）

`generated-hello-123:lifecycle` case：20 次 engine create/close 循环，RSS（MiB）序列：

```
288, 270, 341, 322, 369, 352, 391, 374, 409, 391,
422, 407, 423, 406, 432, 413, 429, 413, 429, 406,
435, 414, 432, 410, 411, 387, 417, 394, 423, 419,
442, 419, 450, 445, 448, 427, 455, 451, 464, 460
```

- residentMinimum: 270.1 MiB（首个 cycle）
- residentMaximum: 464.4 MiB（最后 cycle）
- residentFinal: 460.5 MiB
- **retainedGrowth: +172.1 MiB**（超过 128 MiB 上限）

RSS 在 20 个 create/close cycle 内**持续单调爬升**（270 → 460 MiB），每个 cycle 净增约 4–5 MiB。这是真实的内存累积信号——WebGPU/Dawn 在 Windows D3D12 上反复创建/销毁 session 时，部分 GPU 资源（adapter / device / pipeline cache）未被完全回收。

可能原因与方向（供维护者排查）：
- Dawn D3D12 的 adapter/device/pipeline cache 在 session 销毁后未立即归还给系统；
- ORT WebGPU EP 的 plugin library 卸载/重载路径在 Windows 上有保留；
- OS 文件缓存/driver cache 累积（与 Linux 行为不同，Linux 报告此项通过）。

注意：单次 create（`*-memory` Gate）的 residentMaximum 均 < 350 MiB，`native-cpp-memory` peakResident 仅 252.7 MiB——说明泄漏只在**反复 create/close** 时暴露，单次使用内存正常。

## 6. 阻断性 bug 修复：SHA256SUMS CRLF 污染

本报告基于 origin HEAD `46d52e5`，含阻断性 Windows bug 修复（commit `46d52e5` / 在本报告 revision 中为祖先）。

### 根因

[tools/webgpu/package_bundle.py:168](../../../tools/webgpu/package_bundle.py#L168) 用 `Path.write_text(..., encoding="ascii")` 写 `SHA256SUMS`。Windows 上 `write_text` 默认 `newline=None`，会把 `\n` 翻译成 `\r\n`，产生 CRLF 的 SHA256SUMS。

C++ 校验器 [src/model/model_bundle.cpp:228-252](../../../src/model/model_bundle.cpp#L228) 的 `validate_checksum_inventory` 按 `'\n'` 切行，`line.substr(66)` 得到的 path 末尾**保留 `\r`**。随后 `file_at(files, path)` 用 `det/inference.onnx\r` 查找，map 中实际 key 是 `det/inference.onnx`（不含 `\r`），查找失败抛 `invalid_model_bundle: Required bundle file is missing`。

**后果**：所有 WebGPU bundle 在 Windows 上加载失败，WebGPU 根本无法启动。此 bug 在 Linux CI 不暴露（Linux `write_text` 不转换换行）。

### 修复

`tools/webgpu/package_bundle.py:168` 增加 `newline="\n"`，强制 LF。修复后 bundle 加载成功，WebGPU 真实跑通。

## 7. 诊断性环境补丁（未推送，不影响 PR）

本报告 revision `6970384` = origin `46d52e5` + 一个本地未推 commit `6970384`，仅为绕过本机工具链而加，**受环境变量门控，不改变默认行为**：

[tools/webgpu/qualify.py](../../../tools/webgpu/qualify.py) 新增 `LIGHT_OCR_QUALIFY_GENERATOR=Ninja` 分支：原代码硬编码 `"Visual Studio 17 2022"` generator，本机只有 VS 18 BuildTools（MSVC v180），CMake 报 `MSB8020`。设此环境变量则用 Ninja（vcvars64 就位时直接驱动 cl.exe），并相应调整 `bin/Release/` vs `bin/` 产物路径。默认仍走 VS 17 2022，CI 行为不变。

另外编译时 `CXXFLAGS=/utf-8`（环境变量注入，不改源码/CMakeLists），让 MSVC 按 UTF-8 解析含中日汉字字面量的源文件（[tests/unit/test_ctc.cpp:42](../../../tests/unit/test_ctc.cpp#L42) 等）。建议 PR 的 Windows CI 在 vcvars64 + Ninja + `/utf-8` 下运行，或在 CMakeLists MSVC 分支统一加 `/utf-8`。

## 8. 结论

1. **PR 的 Windows Native WebGPU runtime 契约在 AMD 780M (D3D12) 上基本成立**：provider chain、WebGPU placement、strict fail-closed、14 fixture 质量全对齐、性能（1.21×–2.98×）、单次内存（< 350 MiB）、cold-start（< 1.8 s）全部通过。
2. **唯一阻断是 `repeated-lifecycle` 内存累积**：20 次 create/close 循环 RSS 净增 172 MiB > 128 MiB 上限，RSS 持续单调爬升。疑似 Dawn D3D12 或 ORT WebGPU EP 在反复 session 销毁后未完全回收 GPU 资源；单次使用内存正常。
3. **本轮在 PR 当前流程（已移除非 Mac FP16）下未复现**上一轮基于 FP16 路径时 5 个 allow-quality failed——FP16 路径移除决策与本机观察一致。
4. **不更改 runtime-lock**：维持 `development-pending-device-validation`。建议维护者：
   - 排查 Windows repeated-lifecycle 内存累积根因（Dawn adapter/device 生命周期 / ORT plugin 卸载）；
   - 或评估该上限在 Windows 上的合理性（Linux 与 Windows GPU 资源回收语义不同）。

## 9. 报告产物清单

本次提交包含：

- `reports/webgpu-qualification/windows-x64/qualification-report.json` — 完整 164-Gate 报告
- `reports/webgpu-qualification/windows-x64/qualification-report.sha256` — 报告 sidecar
- `reports/webgpu-qualification/windows-x64/cases/*.json` — 45 个 case 原始结果
- `reports/webgpu-qualification/windows-x64/profiles/*.json` — ORT profile（placement kernel 时间）
- `reports/webgpu-qualification/windows-x64/artifacts/native-runtime-descriptor.json` — 锁定的运行时描述符
- `reports/webgpu-qualification/windows-x64/artifacts/sdk-artifact-manifest.json` — 锁定的 SDK manifest
- `reports/webgpu-qualification/windows-x64/logs/*.log` — 完整命令日志
- `reports/webgpu-qualification/windows-x64/REAL-DEVICE-REPORT.md` — 本报告
