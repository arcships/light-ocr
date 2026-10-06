# NPU SDK 构建与发布

当前实现覆盖 Linux x64 glibc。Intel 使用 OpenVINO C API；AMD 使用独立装载的 Ryzen AI ORT 与预编译 BF16 识别模型。安装后的 OCR 进程无需 Python，系统仍需厂商 NPU 驱动。当前已发布 npm 包未包含这些候选运行时。

## Intel 候选 SDK

```bash
python tools/npu/build_openvino.py \
  --package-cache .cache/npu-packages --output-dir dist/npu-sdk/openvino
```

锁文件 `tools/npu/openvino.lock.json` 固定官方 wheel URL、大小和 SHA-256。工具提取 C headers、OpenVINO core/C API/NPU plugin/ONNX frontend/TBB 及许可证；不会安装 wheel。已有缓存可加 `--offline`。输出目录必须不存在，避免覆盖已锁定产物。

当前 driver/compiler floor 来自本机 OpenVINO 设备属性的数值，尚未覆盖其他代设备。调整配置会要求重新绑定审阅报告。

## AMD 候选 SDK

先自行取得 Ryzen AI 1.8 Linux SDK，并在该 SDK 的 Python 环境执行：

```bash
python tools/npu/compile_amdnpu.py \
  --source-model /path/to/locked-recognition.onnx \
  --source-sha256 <bundle中声明的SHA256> \
  --output-dir dist/amdnpu-models
```

默认配置为 `tools/npu/vaip_config.json`（VAIML BF16）；可用 `--configuration` 指定审阅后的配置。编译和重新打开 context 需要可用的 vendor 栈及设备，尚未在本项目的 AMD 真机上执行。

在安装了 `patchelf` 和 `readelf` 的 Linux 主机导入完整部署目录：

```bash
python tools/npu/import_amdnpu.py \
  --runtime-dir /path/to/vendor/deployment-libraries \
  --models-dir dist/amdnpu-models \
  --license-file /path/to/sdk-license \
  --license-file /path/to/third-party-notices \
  --output-dir dist/npu-sdk/amdnpu
```

部署目录需含 vendor ORT、VitisAI provider 及所有配套原生库。导入工具只允许 OS 和已安装的 NPU 驱动组件留在包外；不安装驱动、不修改系统库或 `LD_LIBRARY_PATH`。应先确认提供的许可证允许再分发。

## 编译 Core 与 Node

```bash
cmake -S . -B build-npu -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLIGHT_OCR_DEPENDENCY_CACHE_DIR="$PWD/.cache/dependencies" \
  -DLIGHT_OCR_OPENVINO_SDK_DIR="$PWD/dist/npu-sdk/openvino" \
  -DLIGHT_OCR_AMDNPU_SDK_DIR="$PWD/dist/npu-sdk/amdnpu" \
  -DLIGHT_OCR_BUILD_NODE=ON \
  -DLIGHT_OCR_NODE_INCLUDE_DIR=/path/to/node/include/node
cmake --build build-npu --parallel 2
```

可省略未取得的 SDK 参数。Node 构建要求 SDK manifest、完整库存和哈希全部有效。产物在 `build-npu/node-runtime`，descriptor 使用 schema 2.1，把 NPU runtime 与基础 ORT 分开声明。JS loader 先验证全部产物，再把包内绝对路径传入 addon；候选排序采用已包含的 `openvino → amdnpu → webgpu → cpu`。

AMD 默认需要 CPU detector，且其模型绑定具体识别模型哈希；换用其他 tier 时需重新编译、审阅对应 SDK。Windows、arm64 和 musl 不接受当前 NPU SDK。

## 审阅报告与正式 SDK

`accept_qualification.py` 只检查审阅结果及其绑定关系，不执行设备实验。每份 JSON 报告应包含：

```json
{
  "schemaVersion": "1.0",
  "provider": "openvino",
  "platformId": "linux-x64",
  "artifactSetSha256": "<sdk-manifest中对应值>",
  "configuration": "<替换为sdk-manifest中完整configuration对象>",
  "deviceFamily": "<实际NPU代际，不能使用机器序列号代替>",
  "reviewedBy": "<审阅人>",
  "evidence": {"report": "<完整质量、性能和失败路径报告及其不可变身份>"},
  "gates": {
    "quality": false, "performance": false, "coldStart": false,
    "cache": false, "lifecycle": false, "failurePaths": false,
    "autoSelection": false, "offlineDistribution": false,
    "placement": false, "redistribution": false
  }
}
```

报告中的门槛需经实际实验及人工审阅后才能填写为 `true`。具体 Intel 门槛见 [Intel Gate](intel-npu-acceleration.md#11-gate)，AMD 待验收项见 [AMD 方案](amd-npu-acceleration.md)。Intel `deviceFamily` 必须使用设备属性 `DEVICE_ARCHITECTURE` 的十进制字符串（如 `5010`），至少需要两个不同架构报告；不能把同代不同机器计作两代。AMD 至少需要一份真实 STX/KRK 报告，family 使用 `STX`、`KRK` 或 `STX/KRK`。

```bash
python tools/npu/accept_qualification.py \
  --sdk-dir dist/npu-sdk/openvino \
  --report /path/to/reviewed-generation-a.json \
  --report /path/to/reviewed-generation-b.json \
  --qualification-id openvino-linux-x64-reviewed-v1 \
  --output-dir dist/npu-release/openvino
```

工具创建新的 SDK 目录，保留所有运行时字节，将报告及其哈希纳入库存，并设置正式资格。原候选保持不变。

## CI 与 npm staging

`NPU native candidates` workflow 生成 Intel SDK 与 Node 候选构建。可输入另一个 run 的 `amdnpu-candidate-sdk` artifact，内容应为 AMD SDK 目录。提供 `npu-reviewed-reports` artifact run ID 时，报告按 `openvino/*.json`、`amdnpu/*.json` 放置；全部审阅门槛通过后才上传 `npu-release-sdks`。

`npm release` workflow 的可选 `npu_sdk_run_id` 读取同仓库的 `npu-release-sdks`（根目录下为 `openvino/`、`amdnpu/`），仅 Linux x64 构建使用。CI 在配置 CMake 前重新验证接受报告及 SDK 字节。

本地 `tools/npm_release.py stage-native` 接受 `--openvino-sdk-dir` 和 `--amdnpu-sdk-dir`；SDK 需与构建 addon 使用的版本、资格 ID 和包含的 provider 一致。候选必须显式添加 `--qualification-build`；正式 `assemble` 始终拒绝候选。许可证、SBOM、SDK manifest 和接受报告随平台包保存。

正式发布仍沿用项目补丁版本策略；当前候选尚未发布到 npm。
