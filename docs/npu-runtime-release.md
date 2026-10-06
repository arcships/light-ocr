# NPU SDK 构建与发布

当前实现覆盖 Linux x64 glibc。Intel 使用 OpenVINO C API；AMD 使用独立装载的 Ryzen AI ORT 与预编译 BF16 识别模型。安装后的 OCR 进程无需 Python，系统仍需厂商 NPU 驱动，AMD 路线使用 Ryzen AI 1.8 支持的 Ubuntu 24.04/glibc 环境。0.5.9 的 Linux x64 发布构建默认附带两套运行时，AMD 默认关闭。

## Intel 候选 SDK

```bash
python tools/npu/build_openvino.py \
  --package-cache .cache/npu-packages --output-dir dist/npu-sdk/openvino
```

锁文件 `tools/npu/openvino.lock.json` 固定官方 wheel URL、大小和 SHA-256。工具提取 C headers、OpenVINO core/C API/NPU plugin/ONNX frontend/TBB 及许可证；不会安装 wheel。已有缓存可加 `--offline`。输出目录必须不存在，避免覆盖已锁定产物。

当前 driver/compiler floor 来自本机 OpenVINO 设备属性的数值，尚未覆盖其他代设备。调整配置会要求重新绑定审阅报告。

## AMD 候选 SDK

常规发布直接取得已锁定的部署包，不需要安装完整 SDK：

```bash
python tools/npu/fetch_amdnpu.py \
  --package-cache .cache/npu-packages --output-dir dist/npu-sdk/amdnpu
```

`amdnpu.lock.json` 锁定归档大小、SHA-256 与完整 artifact set。发布草稿阶段，工具通过已授权的 `GH_TOKEN` 和 `gh release download` 取得附件；公开后直接下载。缓存可使用 `--offline`。官方来源及其原始库哈希保存在 `amdnpu-source.lock.json`。

随包提供 Ryzen AI 1.8/ORT 1.27 的 20 个原生库，以及绑定 Small 0.3.4 的 20 个 EPContext。大型库会增加 Linux x64 包体积；Tiny/Medium 不能复用这些识别 context。附带的 `libpython3.12` 是 vendor 库的原生依赖，终端用户无需安装 Python 解释器。

需要重新编译模型时，从官方 SDK 建立隔离 Python 环境后执行：

```bash
python tools/npu/compile_amdnpu.py \
  --source-model /path/to/locked-recognition.onnx \
  --source-sha256 <bundle中声明的SHA256> \
  --output-dir dist/amdnpu-models
```

默认配置为 `tools/npu/vaip_config.json`（VAIML BF16）；可用 `--configuration` 指定审阅后的配置。编译仅创建 vendor 编译会话并检查嵌入式 EPContext，不执行推理。20 桶已在无 AMD 设备的主机完成编译；设备推理尚未验证。

在安装了 `patchelf` 和 `readelf` 的 Linux 主机导入完整部署目录：

```bash
python tools/npu/import_amdnpu.py \
  --runtime-dir /path/to/vendor/deployment-libraries \
  --models-dir dist/amdnpu-models \
  --license-file /path/to/sdk-license \
  --license-file /path/to/third-party-notices \
  --output-dir dist/npu-sdk/amdnpu
```

部署目录需含 vendor ORT、VitisAI provider 及所有配套原生库。导入工具只允许 OS 和已安装的 NPU 驱动组件留在包外；设置 `$ORIGIN:/opt/xilinx/xrt/lib` RPATH 并清除 vendor ELF 的 executable-stack 标记，原始/变更后哈希均记录；不安装驱动、不修改系统库或 `LD_LIBRARY_PATH`。应先确认提供的许可证允许再分发。

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

可省略未取得的 SDK 参数。Node 构建要求 SDK manifest、完整库存和哈希全部有效。产物在 `build-npu/node-runtime`，descriptor 使用 schema 2.1，把 NPU runtime 与基础 ORT 分开声明。JS loader 先验证全部产物，再把包内绝对路径传入 addon；Auto 排序采用已包含的 `openvino → webgpu → cpu`；AMD 始终不进入 Auto。

AMD 默认需要 CPU detector，且其模型绑定具体识别模型哈希；换用其他 tier 时需重新编译对应 SDK。Windows、arm64 和 musl 不接受当前 NPU SDK。

## 用户显式开启 AMD

含 AMD SDK 与模型的构建默认不会选择或装载 AMD runtime。Node.js 用户在创建引擎时指定：

```js
const engine = await createEngine({
  execution: { provider: "amdnpu" }
});
```

模型无关的 `@arcships/light-ocr-runtime` 还需传入 `bundlePath`；模型 facade 自动提供 bundle。CLI 使用 `--provider amdnpu`；C++ 使用 `options.execution.provider = ExecutionProvider::amdnpu`。省略该配置或指定 `auto` 会走默认候选列表。显式 AMD 请求不静默回退；缺少包内 AMD SDK/模型、设备或驱动时报告对应错误。

发布不要求 NPU 真机报告；`deviceValidated` 保持 `false`。SDK 和真实编译模型通过锁定的部署包自动随构建交付。

## 可选设备报告

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

真机报告不作为 NPU 支持发布前置。下面的报告接受工具仅供以后记录设备证据；使用它时，门槛需经实际实验及人工审阅后才能填写为 `true`。具体实验清单见 [Intel Gate](intel-npu-acceleration.md#11-gate) 和 [AMD 方案](amd-npu-acceleration.md)。Intel `deviceFamily` 必须使用设备属性 `DEVICE_ARCHITECTURE` 的十进制字符串（如 `5010`），至少需要两个不同架构报告；不能把同代不同机器计作两代。AMD 至少需要一份真实 STX/KRK 报告，family 使用 `STX`、`KRK` 或 `STX/KRK`。

```bash
python tools/npu/accept_qualification.py \
  --sdk-dir dist/npu-sdk/openvino \
  --report /path/to/reviewed-generation-a.json \
  --report /path/to/reviewed-generation-b.json \
  --qualification-id openvino-linux-x64-reviewed-v1 \
  --output-dir dist/npu-release/openvino
```

工具创建新的 SDK 目录，保留所有运行时字节，将报告及其哈希纳入库存，并记录 SDK 的设备资格。原候选保持不变；发布不要求执行这一步，运行时也不会仅因已发布就声称 `deviceValidated: true`。

## CI 与 npm staging

`NPU native candidates` workflow 默认取得 Intel 与 AMD SDK 并生成双 NPU Node 候选构建。可输入另一个 run 的 `amdnpu-candidate-sdk` artifact，内容应为 AMD SDK 目录。始终上传可分发的 `npu-release-sdks`，无需设备报告。可选的 `npu-reviewed-reports` artifact 按 `openvino/*.json`、`amdnpu/*.json` 放置；仅在主动提供报告时才检查其绑定关系。

从 0.5.9 发布准备起，`npm release` 默认为 Linux x64 准备锁定的 Intel 与 AMD SDK，缺少任一套均阻止发布构建。可选 `npu_sdk_run_id` 读取同仓库的 `npu-release-sdks`（根目录下为 `openvino/`、`amdnpu/`），仅 Linux x64 构建使用。CI 在配置 CMake 前验证 SDK 库存、模型、配置和哈希；已附带的接受报告仍会校验，但不要求提供报告。

本地 `tools/npm_release.py stage-native` 接受 `--openvino-sdk-dir` 和 `--amdnpu-sdk-dir`；SDK 需与构建 addon 使用的版本、资格 ID 和包含的 provider 一致。NPU SDK 的 `qualificationOnly` 记录其设备证据状态，不再使整个平台包变成 qualification build；无需真机报告即可 staging。WebGPU 自身的既有资格门槛仍适用，正式 `assemble` 仍拒绝基础运行时的 qualification build。许可证、SBOM、SDK manifest 和可选报告随平台包保存。

正式发布仍沿用项目补丁版本策略；当前候选尚未发布到 npm。
