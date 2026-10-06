# AMD NPU 加速技术方案

更新时间：2026-10-06。状态：Linux x64 glibc 后端与打包改造已实现，默认关闭、配置显式开启；Ryzen AI SDK 与编译产物尚缺，真机验收不再作为发布前置。当前已发布 npm 包不含 AMD NPU 运行时。

## 范围与模型路线

首期针对 STX/KRK（Ryzen AI 300、Max 等 XDNA2），使用 BF16 识别模型，检测保持 ORT CPU。PHX/HPT 的 INT8 路线、Windows、musl 均未实现。`precision` 仅接受 `auto`；`cpuPartition=forbid` 不支持此路线。

Ryzen AI 1.8 的 Linux 文档支持 CNN BF16/INT8 编译和执行。旧版预研把 release notes 中 LLM 的 Linux 模型生成限制推广到了 CNN OCR，现予纠正。[AMD Linux 文档](https://ryzenai.docs.amd.com/en/latest/linux.html)、[Release Notes](https://ryzenai.docs.amd.com/en/latest/relnotes.html)。

BF16 路线由 VAIML 编译 FP32 ONNX，生成可嵌入权重与微码的 EPContext；无需引入 INT8 校准集。C++ 部署使用预编译 BF16 context，保留 FP32 输入输出。VitisAI 自动将未支持的算子分配给 CPU，诊断因此明确包含 CPU provider。[AMD 模型编译与部署](https://ryzenai.docs.amd.com/en/latest/modelrun.html)。

## 已完成代码

- `src/inference/amdnpu/backend.cpp`：通过独立 glibc link namespace 装载 AMD 自带 ORT C API 20，避免绑定到本进程的 CPU/WebGPU ORT；AMD runtime 对象始终由对应 API 释放。
- 校验源识别模型哈希、VAIML 配置和 20 个静态宽度桶；识别形状为 `[1,3,48,width]`，批次为 1。编译结果的字典类别数必须与 bundle 一致。
- 设备检查要求可读写的 AMD STX/KRK `/dev/accel` 节点；无设备返回 `adapter_unavailable`。库或模型哈希不符属于不可恢复错误，不能静默跳到 CPU。
- detector 使用 CPU，recognition 使用 `VitisAIExecutionProvider` 和其允许的 CPU 分区。只有首次会话及对应桶实际创建成功后才选中该候选；后续推理失败直接报告错误。
- `tools/npu/compile_amdnpu.py`：在 vendor SDK Python 环境编译所有桶，要求存在嵌入式 VitisAI EPContext，并重新打开部署产物。
- `tools/npu/import_amdnpu.py`：从已取得的 SDK 部署目录导入原生库，设置包内 RPATH、检查 ELF 依赖闭包、记录变更前后哈希，并锁定模型、配置及许可证。
- CMake、Node descriptor 2.1、JS loader、npm staging、SBOM 和候选/正式发布流程均已接入。构建入口与报告格式见 [NPU SDK 构建与发布](npu-runtime-release.md)。

## 默认关闭与发布策略

AMD 始终排除在 Auto 候选列表之外；用户通过 `execution.provider: "amdnpu"`、CLI `--provider amdnpu` 或 C++ 的 `ExecutionProvider::amdnpu` 显式开启。构建必须含 vendor SDK 与匹配的识别模型；运行时未配置 AMD 时不装载 vendor ORT。

按维护者决定，NPU 真机验收不再阻塞发布。SDK 导入的 `qualificationOnly: true` 仅记录尚无设备证据，平台包可随其他已满足条件的运行时一起发布；`deviceValidated` 保持 `false`。库存、模型哈希、依赖闭包和许可证校验继续执行，设备报告可选。

## 尚未验证的设备行为（可选后续工作）

1. 取得 Ryzen AI 1.8 Linux SDK 和 STX/KRK 主机；确认独立 namespace 装载、XRT/amdxdna 驱动及 VitisAI C API 20 能协同工作。
2. 编译真实 PP-OCRv6 模型，核对 20 桶的 NPU 分区与 FP32 CPU goldens；当前没有生成或伪造这些产物。
3. 对完整 locked corpus 记录逐行文本、框与置信度差异，审阅 BF16 误差；测量冷/热延迟、CPU 时间和 RSS。
4. 覆盖 20 次关闭/重建、驱动兼容、权限不足、无设备、损坏产物，以及与 WebGPU 共存和回退。
5. 核实 SDK/模型再分发条款和完整第三方 notices，再创建 accepted SDK。

这些设备行为尚无实测结论，不再作为源码合入或 AMD 显式开启支持发布的前置条件。
