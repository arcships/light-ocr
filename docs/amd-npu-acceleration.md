# AMD NPU 加速技术方案

更新时间：2026-10-08。完整 CPU/NPU 分区后端、20 个识别宽度桶、共享权重和独立支持包构建已实现。AMD 默认关闭，用户通过配置显式开启；无设备软件测试和 CPU 抽样比较已完成，AMD 设备推理仍未验证。

## 使用方式

```sh
npm install @arcships/light-ocr @arcships/light-ocr-amdnpu-linux-x64-gnu
```

在原有引擎配置中设置 `execution: { provider: "amdnpu" }`，或使用 CLI `--provider amdnpu`、C++ `ExecutionProvider::amdnpu`。
支持包需与 Core 版本匹配；0.5.9 仍处于发布准备阶段。

AMD 不参与 Auto，支持包不会自动安装。系统需可读写的 STX/KRK NPU 设备节点、amdxdna 驱动和固件；CI 构建环境为 Ubuntu 24.04 Linux x64 glibc。
不需要完整 Ryzen AI SDK、XRT、Python 或用户机器上的模型编译。PHX/HPT、Windows、arm64、musl 尚未支持。

## 模型与分区

识别模型固定绑定 Small 0.3.4 的原始 ONNX SHA256，保留 batch=1、20 个宽度桶和 FP32 输入输出：
`[1,3,48,width] → [1,width/8,18710]`。原有字典和 CTC 解码继续使用。

| 计算 | 实际执行 |
| --- | --- |
| 19 个输入/输出通道均 ≥128 的 1×1、stride=1 卷积矩阵核心 | IREE AMD AIE / XDNA2 NPU |
| 布局转换、补零、精度转换、bias、GELU、残差 | IREE CPU |
| 其他卷积、attention、分类头和 Softmax | IREE CPU |
| 检测 | ONNX Runtime CPU |

矩阵 M/N/K 按 128 补齐并裁切；单次 dispatch 最多 3840 行，较宽桶分段执行，规避固定编译器的宽矩阵 shim DMA 维度 lowering 越界。
BF16 操作数在 Peano 微内核内转换为 BFP16ebs8，FP32 累加；精度诊断为 `bf16-bfp16ebs8`。
这是完整识别分区图。CPU 转换抽样结果见 [软件验证记录](amd-aie-software-validation-results.json)；尚无 AMD 内核数值与性能结论。
`precision` 请求仍只接受 `auto`；`cpuPartition=forbid` 会拒绝此路线。

## 独立包内容

- 一个小型原生 C ABI 执行库，IREE 与 AMD 驱动符号隐藏。
- 20 个 VMFB，内含 CPU ELF、AMD PDI 和跨设备执行计划。
- 一份共享 IRPA 权重：参数按内容哈希命名，相同权重跨宽度只存一次。
- 匹配 addon、CPU ORT、部署配置、完整哈希库存、许可证和 SBOM。

编译器、Peano、原始 ONNX、临时权重归档和 vendor SDK 库不进入 AMD 包。主 package 与普通平台包不携带这些 AMD 产物。
逐文件大小及包大小见 [部署结果清单](amd-aie-deployment-results.json)。

## 原生行为与诊断

库、配置、共享权重和桶模型均按字节数/SHA256 校验；哈希错误直接报告，不能静默回退。
没有可访问 NPU 时，选择原因是 `adapter_unavailable`，公开 API 返回 `unsupported_capability`，不会静默回退。后续桶按需加载。当前 `sessionFallback="cpu"` 不受支持；使用 Auto 或显式 CPU 可走 CPU 路线。

内部配置为 `target=IREEAMDAIE`、`runtimeAbi=1`；实际识别 provider 链为 `IREEAMDAIE → IREECPU`，检测为 `CPUExecutionProvider`。
会话关闭先释放模型 context，再释放参数模块与设备；动态库保持映射。
`deviceValidated=false`，AMD 数值与性能未验证。40 项无设备软件测试通过；三个代表宽桶及文字样本的 CPU 转换比较通过，BF16 CPU 模拟不能代替 BFP16ebs8 内核验证。详见 [结果](amd-aie-software-validation-results.json) 与 [运行命令](amd-aie-development.md#无设备软件检查)。

## 构建与兼容

入口、锁定工具链与分区实现见 [完整部署开发说明](amd-aie-development.md)；独立包发布流程见 [NPU SDK 构建与发布](npu-runtime-release.md)。
选型、体积预算和实际实施状态见 [轻量部署方案](amd-npu-lightweight-plan.md)。

旧 VAIML/EPContext 部署仍可通过既有 `fetch_amdnpu.py` / `import_amdnpu.py` 工具准备；原生层保留 vendor ORT 独立 API/namespace 路径。
新发布工作流默认从锁定源码构建 IREE 轻量部署，不复用旧 context，也不要求取得专有 SDK。
