# AMD AIE 开发与子图编译

状态：完整识别分区后端、全部宽度桶编译、共享权重和独立包构建已实现。此流程不执行设备推理。
总体选型与后续阶段见 [轻量部署方案](amd-npu-lightweight-plan.md)。
完整部署产物和包大小见 [部署结果清单](amd-aie-deployment-results.json)。
早期子图调查见 [初期编译结果](amd-aie-compilation-results.json)，不能用其中的阶段状态代替当前后端状态。

## 完整部署入口

Python 3.12、CMake ≥3.26、Clang、Ninja、Git、Perl、Make 和 uuid 开发头文件为构建依赖。
以下命令准备锁定源码、Peano、前端环境和原始 Small 模型，构建编译器与执行库，生成全部 20 桶并组装 SDK：

```sh
python3.12 tools/npu/build_amdaie.py \
  --work-dir .cache/amd-aie --output-dir dist/amdaie-sdk --jobs 2
```

输出目录必须不存在。完整模型缓存按锁文件、前端要求和转换脚本身份区分；运行库单独增量构建。
安装用户不需要 Python、编译器、Ryzen AI SDK、XRT 或联网模型编译；仍需可用的系统 amdxdna 驱动、固件和 STX/KRK NPU。
CI 在 Ubuntu 24.04 构建，系统动态依赖包括 libuuid、libstdc++、libgcc_s、libm、libc。

### 分区与数据流

- 完整图输入 `[1,3,48,width]`，输出 `[1,width/8,18710]`，保留 FP32 ABI 和原有 CTC。
- 19 个输入/输出通道均 ≥128 的 1×1、stride=1 卷积转换成 BF16 矩阵乘。M/N/K 补齐到 128 的倍数，计算后裁切有效空间和通道。
- 布局转换、补零、FP32→BF16、bias、GELU、残差、其他卷积、attention、分类头和 Softmax 使用 IREE CPU；检测继续用 ORT CPU。
- Peano 微内核把 BF16 转为 BFP16ebs8 后计算，累加输出为 FP32。不能宣称纯 BF16 数值、推理正确或已有加速比。
- `partition_iree_model.py` 隔离矩阵计算并阻止零初始化跨 dispatch 共享；`route_iree_dispatches.py` 在 Flow 形成后赋予明确 CPU/NPU affinity，避免早期 affinity 被优化丢失。
- 每个 VMFB 同时含 CPU ELF、AMD PDI 和跨设备调度。CPU 可以映射 AMD host-only BO；共享分配策略选择 NPU 分配器，双方的映射、缓存同步和 fence 由 HAL 管理。
- 权重使用内容哈希作为参数 key；构建工具合并相同参数，所有桶共用 `models/recognition.irpa`。编译器、Peano、各桶的临时 IRPA 和原始 ONNX 不随 AMD 包分发。

### 原生运行接口

`src/inference/amdnpu/runtime_api.h` 定义版本化 C ABI；运行库只导出 `LightOcrAieGetApi`，IREE 和驱动符号隐藏。
会话按需加载宽度桶；运行库持有权重、CPU/NPU 设备、参数模块和 VM context。
原生 `AmdNpuSession` 串行执行共享引擎调用，核对库、部署配置、共享权重和模型的字节数及 SHA256。
关闭时先释放全部桶会话，再释放引擎；库保持映射，避免卸载线程局部状态。

新配置 `target=IREEAMDAIE`、`runtimeAbi=1`，与既有 VAIML context 明确区分。
旧 vendor 部署仍由其独立 ORT API/namespace 加载。两种部署共享公开 provider `amdnpu`，新包实际 provider 链为 `IREEAMDAIE → IREECPU`。
`precision` 诊断为 `bf16-bfp16ebs8`，`deviceValidated=false`。

### 独立支持包

```sh
cmake -S . -B build-amdaie -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLIGHT_OCR_DEPENDENCY_CACHE_DIR="$PWD/.cache/dependencies" \
  -DLIGHT_OCR_AMDNPU_SDK_DIR="$PWD/dist/amdaie-sdk" \
  -DLIGHT_OCR_BUILD_NODE=ON -DLIGHT_OCR_BUILD_TESTS=OFF -DLIGHT_OCR_BUILD_TOOLS=OFF \
  -DLIGHT_OCR_NODE_INCLUDE_DIR=/path/to/node/include/node
cmake --build build-amdaie --parallel 2
python tools/generate_release_metadata.py --build-dir build-amdaie \
  --output-dir reports/amdaie --platform-id linux-x64 --model-free
python tools/npu/package_support.py --provider amdnpu --build-dir build-amdaie \
  --metadata-dir reports/amdaie --sdk-dir dist/amdaie-sdk --output-dir dist/amdaie-package
npm pack ./dist/amdaie-package --ignore-scripts --pack-destination dist
```

AMD 不自动安装、不参与 Auto。显式配置 `execution.provider="amdnpu"` 后才加载；`cpuPartition=forbid` 拒绝分区模型。
`npu-support-release.yml` 和 `npu-native.yml` 默认使用轻量源码构建入口，保留 Intel 路线。
按维护者要求，本轮只执行生成部署产物所需的构建与编译，没有运行测试、数值比较或设备推理。

## 早期编译调查（历史记录）

以下记录说明从整图直接下放转为明确 CPU/NPU 分区的原因。未完成的是早期整图全部 NPU 的 lowering，不是当前完整分区后端。

## 构建输入

`tools/npu/iree-source.lock.json` 锁定 AMD AIE、IREE、LLVM 与必要子模块提交，以及 Peano wheel 的大小和 SHA256。
工具只读取已锁定的 Small 0.3.4 识别模型，拒绝源模型哈希不符。
编译工具保存在 `.cache/amd-aie/`，不进入运行时包。AMD 支持包只装入运行库、全部 VMFB、单份共享 IRPA、清单与许可证。

## 准备源码

以下 checkout 发生在独立开发源码目录。工具按锁文件获取所需子模块：

```sh
python tools/npu/fetch_iree_source.py \
  --output-dir .cache/amd-aie/source
```

本机需要 CMake、Ninja、Clang/Clang++、Git、Python、Perl、Make 与相关系统开发头文件。
上游构建还会准备 OpenSSL。Peano 只用于构建，下载与解包命令为：

```sh
python tools/npu/fetch_peano.py \
  --package-cache .cache/amd-aie/downloads \
  --output-dir .cache/amd-aie/peano
```

已有缓存时可加 `--offline`。输出目录必须不存在；工具检查归档身份、拒绝重复路径和链接，并保留执行权限。

## 构建编译器

```sh
python tools/npu/build_iree.py \
  --source-dir .cache/amd-aie/source \
  --peano-dir .cache/amd-aie/peano/llvm-aie \
  --build-dir .cache/amd-aie/toolchain-build \
  --jobs 2
```

构建源树提交必须与锁文件相符。编译器启用 AMD AIE 和 LLVM CPU 后端；关闭单元测试、设备测试、示例及 Python bindings。
上游 CMake 会原地修改 aie-rt、bootgen 与 mlir-air；锁文件同时固定这些配置补丁的 diff 哈希，拒绝其他已跟踪源码改动。
本地 matmul 索引映射边界补丁也按补丁文件与最终源码 diff 双重锁定，由构建工具应用。
`toolchain-build.json` 记录固定提交、配置/构建命令及状态。失败后可在同一个构建目录继续增量构建。
`--configure-only` 仅生成构建配置，不能用于后续子图编译的成功证明。
增量构建在配置命令、源码提交与主机工具一致时复用 CMake 配置，避免上游配置步骤反复改写头文件造成大规模重编译。

## 导出真实识别热点块

导出对象为 `Conv.49 → bias → GELU → Conv.50 → bias → residual`。
FP32 ONNX 保留原始子图；MLIR 将 1×1 卷积映射为矩阵乘，转换为 NHWC，使用 BF16 操作数与 FP32 累加。
GELU 保留 Erf 公式，数值变化尚未验证。

导出环境需要 numpy 与 onnx；可使用现有模型构建 Python 环境。

```sh
python tools/npu/export_iree_hotspot.py \
  --source-model models/generated/ppocrv6-small-onnx-20260714.2/rec/inference.onnx \
  --output-dir .cache/amd-aie/hotspot-384 \
  --width 384
```

输出包括：

- `reference-fp32.onnx`：对应源模型的独立 FP32 子图。
- `projection.mlir`：首个投影层和 bias，用于定位基础映射缺口。
- `projection-matmul.mlir`：真实首层的矩阵核心，BF16 矩阵输入、FP32 输出；布局、输入转换和 bias 在接口之外。
- `projection-matmul-padded.mlir`：矩阵核心按 128 行向上补齐；调用方补零，使用后裁切有效行。
- `mlp-residual.mlir`：完整 MLP 残差块。
- `weights-fp32.npz`：原始矩阵权重与 bias。
- `hotspot-manifest.json`：源模型/产物哈希、宽度、形状、节点及精度转换记录。

宽度 384 时块输入输出为 `[1,384,3,96]`。可使用既有 20 桶内的其他宽度，但每次使用新的输出目录。

## 完整识别图前端

前端单独使用 Python 3.12 环境，依赖不会进入运行时包：

```sh
python3.12 -m venv .cache/amd-aie/host-compiler-venv
.cache/amd-aie/host-compiler-venv/bin/pip install \
  -r tools/npu/iree-frontend.requirements.txt
.cache/amd-aie/host-compiler-venv/bin/python tools/npu/export_iree_model.py \
  --source-model models/generated/ppocrv6-small-onnx-20260714.2/rec/inference.onnx \
  --output-dir .cache/amd-aie/model-384 --width 384
python tools/npu/import_iree_model.py \
  --model-dir .cache/amd-aie/model-384 \
  --frontend-environment .cache/amd-aie/host-compiler-venv \
  --output-dir .cache/amd-aie/model-384-imported
```

导出工具固定输入尺寸、折叠已知形状查询，再用 ONNX 官方转换器将 opset 11 转为 17。
导入流程保留 FP32，将图降为 core Linalg，把普通卷积和深度卷积改为 NHWC，保持模型接口不变。
宽度 384 的输入为 `[1,3,48,384]`，输出为 `[1,48,18710]`；已完成 43 个普通卷积和 14 个深度卷积转换。
池化等其他操作仍可能使用 NCHW。`import-report.json` 保存步骤、日志、工具身份和输出哈希。

标准前端版本 3.12.0 比锁定的 AMD IREE 源码新。输出文本 IR 尚需 AMD 编译器接受；
不能据此认定两版 VMFB 或运行时兼容。完整图已通过锁定 AMD 编译器的 `--stage sources`，
生成 executable sources，证明前端文本 IR 可被该版本接受；尚未通过 AMD 内核 lowering。
完整 FP32 图已用标准 IREE CPU 后端编译，产物为
21,344,236 字节，但未执行推理，也未验证精度。该产物不作为 AMD 模型发布。

## 独立运行时构建

```sh
python tools/npu/build_iree.py \
  --source-dir .cache/amd-aie/source \
  --peano-dir .cache/amd-aie/peano/llvm-aie \
  --build-dir .cache/amd-aie/runtime-build \
  --runtime-only --jobs 2
```

运行时与编译器使用不同构建目录。当前 Linux x86_64 Release 构建的 `iree-run-module`
为 1,843,488 字节，已链接 amdxdna 驱动；ELF 动态依赖为 libm、libuuid、libstdc++、libgcc_s、libc。
它不依赖 vendor SDK 或 XRT。这只是独立执行工具的大小，完整支持包还需 addon、模型及其他运行时文件。
本阶段未运行该工具访问设备。

## 编译与保留诊断

先进行目标 IR lowering：

```sh
python tools/npu/compile_iree_hotspot.py \
  --hotspot-dir .cache/amd-aie/hotspot-384 \
  --toolchain-build-dir .cache/amd-aie/toolchain-build \
  --peano-dir .cache/amd-aie/peano/llvm-aie \
  --output-dir .cache/amd-aie/compile-384-targets \
  --stage targets
```

工具要求完成固定版本构建，并检查输入 MLIR 与 manifest 的哈希。
首期显式指定 `npu4`、2×2 核阵列、`amdxdna` HAL、objectFifo 与 pack-peel，不使用 vendor ORT、XRT HAL 或设备探测。
输出 `compile-report.json` 及每个子图的日志。编译失败或超时返回非零状态，保留诊断；不能将失败描述为 CPU 回退后编译成功。

目标 IR 完成后，使用新的输出目录及 `--stage binary` 尝试生成 VMFB。
`--variant` 可单独选择完整块、投影层或矩阵核心；`--timeout` 限制每次编译的秒数。
VMFB 才属于后续加载候选；目标 MLIR 不作为部署模型打包。

同一工具支持把 `--hotspot-dir` 换成 `--model-dir .cache/amd-aie/model-384-imported`，
尝试完整 FP32 图的 AMD 编译，并保留 `recognition.log`。此时不选择热点 `--variant`；
图导入成功不预设 AMD 编译成功，失败不会自动改用 CPU 后端。

### 已编译的诊断矩阵核心

宽度 384 的原始矩阵为 `288×384 · 384×768`。直接目标编译可通过，但二进制链接需要上游未提供的
`48×32×128` 微内核实例。显式补齐输入到 `384×384`、保留原权重后可编译成 AMD VMFB：

```sh
python tools/npu/compile_iree_hotspot.py \
  --hotspot-dir .cache/amd-aie/hotspot-384 \
  --toolchain-build-dir .cache/amd-aie/toolchain-build \
  --peano-dir .cache/amd-aie/peano/llvm-aie \
  --output-dir .cache/amd-aie/compile-384-matmul-binary \
  --variant projection-matmul-padded --kernel-mode peano-bf16 --stage binary
```

补丁后通过开发脚本生成的产物为 612,008 字节。该文件只覆盖矩阵核心，不包含 bias、GELU、残差、输入布局转换或裁切。
调用方须补零最后 96 行，裁切输出至前 288 行，再执行原 bias。
Peano 微内核内部将 BF16 转为 `bfp16ebs8` 块浮点后计算；这项精度变化尚未验证。
普通向量 lowering 当前不支持 npu4 的 BF16 矩阵指令类型，因此此处显式启用微内核，并关闭 Chess。
`--core-rows` / `--core-cols` 可改变阵列大小，默认 2×2 来避开本例更大阵列的分块及内存问题。

完整投影层在布局转换/FP32→BF16 的独立 dispatch 处报 `Unhandled pass pipeline in setRootConfig`。
这些诊断不能作为完整识别模型已支持的结论；后续仍需处理 CPU/NPU 分区或补齐对应 lowering。

完整 FP32 图进入 AMD lowering 后发生 SIGSEGV；systemd-coredump 回溯指向
`AffineMap::getResult → AMDAIE::isMatmul → initAIELaunchConfig`。
同一时段内核日志没有 OOM 记录，仍有充足可用内存。
源码中 matmul 识别器按无符号 `nResults - 2` 访问索引映射，尚未检查结果数；
补上边界检查后，整图越过该阶段，随后在 `PackOp::createTransposedClone → packTranspose → AMDAIEPackAndTransposePass`
再次发生 SIGSEGV。第二处崩溃与具体触发算子仍待缩减样例确认。

## 结果解释

- 导出完成：证明生成了固定源模型的开发输入。
- executable sources 完成：证明前端 IR 和公共图 lowering 被锁定编译器接受，不证明 AMD 内核可编译。
- 目标 lowering 完成：证明指定编译路径接受了该输入。
- 二进制完成：证明生成了加载候选，不证明设备兼容或数值正确。
- `deviceValidated` 与 `numericsValidated` 在该流程中始终为 `false`。

后续依据实际错误决定布局/分块改造、算子补齐或调整分区，不能为了通过编译删除 GELU、残差或更改字典类别。
此开发入口不替换现有 `amdnpu` 后端，不改变默认 provider，不修改发布包和版本号。
