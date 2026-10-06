# AMD NPU 加速技术方案

更新时间：2026-10-06。状态：Linux x64 glibc 后端与打包改造已实现，默认关闭、配置显式开启；0.5.9 独立支持包的构建输入已包含 Ryzen AI 1.8 的原生部署库与 Small 0.3.4 的 20 个真实 BF16 context。无需真机即可完成编译，尚未执行 AMD 设备推理。正式 npm 发布仍待完成。

## 范围与模型路线

首期针对 STX/KRK（Ryzen AI 300、Max 等 XDNA2），使用 BF16 识别模型，检测保持 ORT CPU。PHX/HPT 的 INT8 路线、Windows、musl 均未实现。`precision` 仅接受 `auto`；`cpuPartition=forbid` 不支持此路线。

Ryzen AI 1.8 的 Linux 文档支持 CNN BF16/INT8 编译和执行。旧版预研把 release notes 中 LLM 的 Linux 模型生成限制推广到了 CNN OCR，现予纠正。[AMD Linux 文档](https://ryzenai.docs.amd.com/en/latest/linux.html)、[Release Notes](https://ryzenai.docs.amd.com/en/latest/relnotes.html)。

BF16 路线由 VAIML 编译 FP32 ONNX，生成可嵌入权重与微码的 EPContext；无需引入 INT8 校准集。C++ 部署使用预编译 BF16 context，保留 FP32 输入输出。VitisAI 自动将未支持的算子分配给 CPU，诊断因此明确包含 CPU provider。[AMD 模型编译与部署](https://ryzenai.docs.amd.com/en/latest/modelrun.html)。

## 已完成代码

- `src/inference/amdnpu/backend.cpp`：通过独立 glibc link namespace 装载 AMD 自带 ORT C API 20，避免绑定到本进程的 CPU/WebGPU ORT；AMD runtime 对象始终由对应 API 释放。
- 校验源识别模型哈希、VAIML 配置和 20 个静态宽度桶；识别形状为 `[1,3,48,width]`，批次为 1。编译结果的字典类别数必须与 bundle 一致。
- 设备检查要求可读写的 AMD STX/KRK `/dev/accel` 节点；无设备返回 `adapter_unavailable`。库或模型哈希不符属于不可恢复错误，不能静默跳到 CPU。
- detector 使用 CPU，recognition 使用 `VitisAIExecutionProvider` 和其允许的 CPU 分区。只有首次会话及对应桶实际创建成功后才选中该候选；后续推理失败直接报告错误。
- `tools/npu/compile_amdnpu.py`：在 vendor SDK Python 环境编译所有桶，要求存在嵌入式 VitisAI EPContext，并检查输入输出类型与形状；编译过程不执行模型推理，不要求真机验收。
- `tools/npu/import_amdnpu.py`：从已取得的 SDK 部署目录导入原生库，设置包内 RPATH、检查 ELF 依赖闭包、记录变更前后哈希，并锁定模型、配置及许可证。
- CMake、Node descriptor 2.1、JS loader、npm staging、SBOM 和候选/正式发布流程均已接入。构建入口与报告格式见 [NPU SDK 构建与发布](npu-runtime-release.md)。

## 默认关闭与发布策略

AMD 始终排除在 Auto 候选列表之外；用户通过 `execution.provider: "amdnpu"`、CLI `--provider amdnpu` 或 C++ 的 `ExecutionProvider::amdnpu` 显式开启。0.5.9 独立 AMD 支持包附带 vendor SDK 与匹配的 Small 识别模型，普通平台包不包含它们；运行时未配置 AMD 时不装载 vendor ORT。

按维护者决定，NPU 真机验收不再阻塞发布。SDK 导入的 `qualificationOnly: true` 仅记录尚无设备证据，平台包可随其他已满足条件的运行时一起发布；`deviceValidated` 保持 `false`。库存、模型哈希、依赖闭包和许可证校验继续执行，设备报告可选。

## 尚未验证的设备行为（可选后续工作）

1. 在 STX/KRK 主机确认独立 namespace 装载、XRT/amdxdna 驱动及 VitisAI C API 20 协同工作。
2. 比较已编译 20 桶模型与 FP32 CPU goldens，记录文本、框、置信度及 BF16 误差。
3. 测量冷/热延迟、CPU 时间和 RSS，覆盖关闭/重建、权限不足、驱动兼容及 WebGPU 共存。

SDK 的原始与打包后哈希、依赖闭包、许可证/第三方 notices 和源模型绑定已随部署包记录；这不等同于真机验收或独立法律审阅。
这些设备行为尚无实测结论，不再作为源码合入或 AMD 显式开启支持发布的前置条件。
