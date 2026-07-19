# light-ocr Core 构建与发布

状态：已实现；Core Tier 1 与 npm `0.2.0` 发布证据已产生
范围：C++ Core 的依赖锁、构建、测试、验证报告和发布候选制品  
需求：[requirements.md](requirements.md)  
当前状态：[implementation-status.md](implementation-status.md)

## 1. 交付边界

当前交付物是 C++17 静态库 `light_ocr_core`、三个标准库公共头文件、验证工具、真实 PP-OCRv6 模型 bundle 和验收报告。Core 运行时不包含 Python；Python 仅用于测试 oracle、语料生成和发布元数据。

Core 交付仍不是稳定 ABI、公共 C ABI 或 C++ 包管理器 SDK。Node.js 用户可以使用已发布的 `@arcships/light-ocr@0.2.0` 与四平台 prebuild，但这不改变 Core 的源码集成边界。外部 C++ 安装布局属于 D102；因此仓库当前不提供容易被误认为完整 SDK 的 `cmake --install` 规则。仓库内 C++ 消费者应通过 `add_subdirectory` 使用 `light_ocr::core`，发布验证包主要服务验收，不构成长期二进制兼容承诺。

```cmake
add_subdirectory(path/to/light-ocr)
target_link_libraries(your_target PRIVATE light_ocr::core)
light_ocr_stage_onnxruntime(your_target)
```

最后一行把锁定的 ONNX Runtime 动态库放到消费者可执行文件旁，并设置 macOS/Linux 的相对 RPATH；在 D102 完成前，它是源码集成约定，不是已安装 package config API。

## 2. Tier 1 与工具链

| 平台 | GitHub runner | 当前约束 |
| --- | --- | --- |
| macOS arm64 | `macos-15` | 最低 macOS 13.3；由 ONNX Runtime 1.22.0 二进制决定 |
| macOS x64 | `macos-15-intel` | 最低 macOS 13.3 |
| Windows x64 | `windows-2022` | MSVC 2022，Windows x64 ONNX Runtime |
| Linux x64 | `ubuntu-24.04` | 以该 runner 产出的 glibc/CPU 需求为准 |

macOS 的 `CMAKE_OSX_DEPLOYMENT_TARGET` 默认固定为 `13.3`。当前没有对 Linux 更低 glibc 版本作未验证承诺；如将来发布通用 Linux SDK，应在 D102 中另行选择构建容器和 CPU baseline。

构建要求：

- CMake 3.24 或更高；本地 preset 不绑定生成器；Unix CI 使用 Ninja，Windows CI 使用 Visual Studio 2022 x64 generator。
- C++17，关闭编译器扩展。
- 项目源码强警告并视为错误。
- Clang/GCC 使用 `-ffp-contract=off`；MSVC 使用 `/fp:strict`；禁止 fast-math。
- Release 用于对齐、质量和性能验收；Debug 用于 Sanitizer 和 fuzz。

## 3. 已锁定的原生依赖

`models/deps.lock.json` 是唯一版本与归档身份来源：

| 依赖 | 锁定版本 | 用途 |
| --- | --- | --- |
| ONNX Runtime | 1.22.0 | CPU Execution Provider |
| OpenCV | 4.10.0 | 仅 `core`、`imgproc`；静态构建 |
| Clipper | 6.4.2，来自 pyclipper 1.3.0.post6 | 与 PaddleOCR 的 pyclipper 整数 offset 行为一致 |
| stb | commit `31c1ad374564` | Node adapter 的内存 JPEG/PNG 解码；关闭 stdio 和其他格式 |
| nlohmann/json | 3.11.3 | 有界 bundle JSON 解析 |

OpenCV 同时带入锁中声明的 zlib 1.3.1 与 Carotene 0.0.1。项目自己的 SHA-256 实现用于 bundle 完整性。

`tools/bootstrap_dependencies.py` 校验归档字节数、SHA-256，并在交给 CMake 前拒绝绝对路径、`..`、重复成员、符号/硬链接、设备和未知成员类型。发布构建先联网填充缓存，再执行一次 `--offline` 校验；后续 CMake 配置只使用该缓存。

Native WebGPU 是独立 runtime flavor，不修改上述 CPU lock。其 authority 为 [`tools/webgpu/runtime-lock.json`](../tools/webgpu/runtime-lock.json)：精确锁定 ORT Core 1.24.4、official WebGPU Plugin EP 0.1.0、NuGet URL/catalog/bytes/SHA-512、Linux Vulkan 与 Windows D3D12 payload、headers、licenses 和 session options。[`tools/webgpu/build_runtime.py`](../tools/webgpu/README.md) 从锁定 NuGet 组装 exact SDK，并支持同一 package cache 的离线复装；CMake 使用 SDK 前再次验证完整 inventory/hash。

## 4. 首次准备

```bash
python3 tools/bootstrap_dependencies.py --cache-dir .cache/dependencies
python3 tools/bootstrap_dependencies.py --cache-dir .cache/dependencies --offline

python3 tools/bootstrap_models.py --cache-dir .cache/models
python3 tools/package_model_bundle.py

python3 corpus/generate_corpus.py \
  --cache-dir .cache/corpus \
  --output-dir corpus/fixtures
```

模型 bootstrap 是显式步骤；正常编译和 Core 运行都不会下载模型。`package_model_bundle.py` 生成确定性的 USTAR 归档并核对 `models/bundles.lock.json` 中的最终字节数和 SHA-256。

## 5. 本地构建

仅 C++ 测试：

```bash
cmake --preset release \
  -DLIGHT_OCR_DEPENDENCY_CACHE_DIR="$PWD/.cache/dependencies"
cmake --build --preset release --parallel
ctest --preset release
```

启用完整 Python oracle：

```bash
python3.11 -m venv .cache/oracle-venv
.cache/oracle-venv/bin/python -m pip install \
  --require-hashes -r oracle/requirements.lock

cmake --preset release \
  -DLIGHT_OCR_DEPENDENCY_CACHE_DIR="$PWD/.cache/dependencies" \
  -DLIGHT_OCR_ORACLE_PYTHON="$PWD/.cache/oracle-venv/bin/python" \
  -DLIGHT_OCR_PARITY_LIVE_ORACLE=ON
cmake --build --preset release --parallel
ctest --preset release
```

可用 preset：`dev`、`release`、`asan`、`tsan`、`fuzz`。Apple Clang 若没有 libFuzzer runtime，`fuzz` preset 会明确退化为固定 seed 的 standalone smoke driver；Linux CI 使用完整 Clang/libFuzzer。

Node-API 是默认关闭的可选 target。开发构建需显式提供 Node headers，并使用调用方已有的本地模型 bundle；发布 package 则由 facade 注入随 npm 安装的默认 model bundle。完整命令、API 和取消/生命周期说明见 [bindings/node/README.md](../bindings/node/README.md)。启用 `LIGHT_OCR_BUILD_NODE=ON` 和 `LIGHT_OCR_BUILD_TESTS=ON` 后，CTest 会在 Node executable 与生成 bundle 均存在时注册 `light_ocr_node_tests`。

WebGPU qualification build 先组装目标 SDK，再显式选择 flavor：

```bash
python3 tools/webgpu/build_runtime.py \
  --platform linux-x64 \
  --package-cache .cache/webgpu-runtime/packages \
  --output-dir dist/webgpu-sdk/linux-x64

cmake -S . -B build-webgpu -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLIGHT_OCR_DEPENDENCY_CACHE_DIR="$PWD/.cache/dependencies" \
  -DLIGHT_OCR_ONNXRUNTIME_FLAVOR=webgpu \
  -DLIGHT_OCR_WEBGPU_SDK_DIR="$PWD/dist/webgpu-sdk/linux-x64" \
  -DLIGHT_OCR_WEBGPU_QUALIFICATION_BUILD=ON
```

pending lock 只能用于 qualification build；普通 release configure 要求双平台 Provider Gate 已接受，且 lock 中本平台 `qualifiedArtifactSetSha256` 与 SDK 完全一致。真实 Linux/Windows GPU 的完整构建、npm staging、14-fixture placement/质量/性能/生命周期和报告回收统一执行：

```bash
python3 tools/webgpu/qualify.py
```

## 6. 构建目标

| 目标 | 内容 |
| --- | --- |
| `light_ocr_core` / `light_ocr::core` | C++17 静态 Core |
| `light_ocr_validate` | 单个 raw-pixel 输入的 JSON 结果 |
| `light_ocr_stage_probe` | 测试专用全阶段记录 |
| `light_ocr_benchmark` | load、初始化、各阶段、总延迟和 RSS |
| `light_ocr_memory_gate` | 独立进程高分辨率 resize、tensor-shape、文本框和 absolute peak RSS 门槛；不依赖 Python |
| `light_ocr_leak_check` | 默认重复完整 engine 生命周期；`--reuse-engine` 测量单 engine 连续处理页面的 RSS 增长 |
| `light_ocr_unit_tests` | 算法、边界和错误契约 |
| `light_ocr_integration_tests` | 真实模型、golden、并发、关闭和 ORT 错误映射 |
| `light_ocr_fuzz_*` | image、bundle、geometry、lifecycle 四个 fuzz 入口 |
| `light_ocr_node` | 可选 Node-API v8 addon；需要 `LIGHT_OCR_BUILD_NODE=ON` 与显式 Node headers |

所有需要 ONNX Runtime 的项目可执行文件在构建后会把动态运行库放到自身目录。macOS 使用 `@loader_path`，Linux 使用 `$ORIGIN`，Windows 将 `onnxruntime.dll` 放在 `.exe` 旁边；这样验证工具不依赖构建缓存的绝对路径。

## 7. 验证命令

全阶段和质量：

```bash
ctest --test-dir build/preset-release --output-on-failure -L acceptance
```

Sanitizer 和 fuzz 使用独立 build tree，不能把 ASan/UBSan 与 TSan 混在同一构建：

```bash
cmake --preset asan -DLIGHT_OCR_DEPENDENCY_CACHE_DIR="$PWD/.cache/dependencies"
cmake --build --preset asan --parallel
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 ctest --preset asan

cmake --preset tsan -DLIGHT_OCR_DEPENDENCY_CACHE_DIR="$PWD/.cache/dependencies"
cmake --build --preset tsan --parallel
TSAN_OPTIONS=halt_on_error=1 ctest --preset tsan

cmake --preset fuzz -DLIGHT_OCR_DEPENDENCY_CACHE_DIR="$PWD/.cache/dependencies"
cmake --build --preset fuzz --parallel
build/preset-fuzz/bin/light_ocr_fuzz_image -runs=100000
build/preset-fuzz/bin/light_ocr_fuzz_bundle -runs=100000
build/preset-fuzz/bin/light_ocr_fuzz_geometry -runs=100000
LIGHT_OCR_MODEL_BUNDLE="$PWD/models/generated/ppocrv6-small-onnx-20260714.2" \
  build/preset-fuzz/bin/light_ocr_fuzz_lifecycle -runs=10 -max_len=64
```

macOS 的系统 Apple Clang 通常没有 libFuzzer runtime，因此本地 `fuzz` 可能是确定性 standalone driver，且 ASan 设置 `detect_leaks=0`；Linux safety CI 使用真正的 libFuzzer 与 LSan。

性能门槛：

```bash
.cache/oracle-venv/bin/python oracle/run_benchmark.py \
  --native-benchmark build/preset-release/bin/light_ocr_benchmark \
  --bundle models/generated/ppocrv6-small-onnx-20260714.2 \
  --fixture corpus/fixtures/generated-hello-123/fixture.json \
  --warmup 5 --iterations 30 \
  --report reports/benchmark/macos-arm64.generated-hello-123.json
```

重复生命周期：

```bash
build/preset-release/bin/light_ocr_leak_check \
  --bundle models/generated/ppocrv6-small-onnx-20260714.2 \
  --pixels corpus/fixtures/generated-hello-123/pixels.bin \
  --width 800 --height 180 --stride 2400 --format bgr8 \
  --warmup 5 --iterations 10 \
  --report reports/leak/macos-arm64.generated-hello-123.json
```

RSS gate 在 glibc 平台的基线和每个测量周期后调用 `malloc_trim` 请求归还未使用页。这只作用于测试进程，避免把 Linux allocator cache 波动当成 Core 对象泄漏；仍存活或不可释放的分配继续计入 RSS，32 MiB 总增长和 8 MiB/周期门槛不变。其他平台继续依靠预热后的原生 RSS。

无 cwd、locale、隐式环境依赖：

```bash
python3 tools/run_offline_check.py \
  --validate build/preset-release/bin/light_ocr_validate \
  --bundle models/generated/ppocrv6-small-onnx-20260714.2 \
  --fixture corpus/fixtures/generated-hello-123/fixture.json
```

Linux CI 还把同一命令放入 `unshare --net` 网络命名空间并要求 `--require-network-disabled`。

macOS arm64 高分辨率绝对 RSS gates 由 `light_ocr_memory_gate` 独立进程运行，CTest 名称为 `light_ocr_memory_blank_2048` 和 `light_ocr_memory_dense_2048`，因此 Tier 1 job 不需要安装 Python oracle。报告固定 `[1,3,960,960]` detection shape、文本框数、每个 recognition batch shape 与 peak RSS；`oracle/run_memory_gate.py` 保留为 benchmark JSON 的交叉检查包装器。其他 Tier 1 平台先保存本平台 baseline，再执行 15% 回归门槛。

## 8. CI

`.github/workflows/core.yml` 定义三类 job：

- `tier1`：四个 Tier 1 原生 runner，锁定依赖/模型、离线缓存复核、Release 构建、真实模型测试、sterile/offline 检查、RSS gate、manifest/license/SBOM。
- `safety`：Linux ASan+UBSan+LSan、TSan、四个 libFuzzer 入口。
- `oracle`：hash-locked Python 环境、committed corpus/golden 身份校验、同机 live oracle 的 14 个语料全阶段对齐和首 bundle 质量基线。

`.github/workflows/npm-release.yml` 是仅允许从 `main` 手动触发的发布候选与发布流程。默认 `publish_to_registry=false`，所以第一次运行不会读取 `NPM_TOKEN` 或改动 npm registry：

- 0.3.0 候选先在 Linux/Python 3.11 的哈希锁工具链中重现并校验内部 WebGPU FP16 派生工件，再在 macOS/Python 3.12 中派生固定 Core ML FP16 package hashes，最后把合并的 native superset bundle 交给 Linux assemble；WebGPU 公共执行 profile 只发布 FP32，用户安装、postinstall 和首次运行都不会执行转换或联网。
- 四个平台分别原生构建 Node-API addon，并保存许可证与 SPDX SBOM。
- 汇聚为一个 facade、一个 model 和四个 native packages，执行两次 `npm pack` 并要求 tarball SHA-256 完全一致。
- 在 macOS arm64/x64、Linux x64 glibc、Windows x64 上分别使用 Node.js 22 和 24，从本地 tarballs 执行 `--ignore-scripts` 安装、CJS/ESM bounded OCR、单次 tiled contract/结果 smoke 与 TypeScript compile test。
- 六个 tarball 先发布到一次性 Verdaccio registry，只安装 facade 后停止 registry，再执行真实 bounded 与 tiled OCR，证明没有运行时下载依赖。
- 只有以上功能/制品 gates、需要时已经单独完成的受审 baseline，以及 `publish_to_registry=true` 同时满足时，`npm-release` GitHub environment 才能读取 `NPM_TOKEN`；先发布五个依赖到 `next`，通过 registry facade 安装后再发布主包，最终禁网运行并可显式提升到 `latest`。

首个版本的触发命令为：

```bash
gh workflow run "npm release" --ref main \
  -f version=0.2.0 \
  -f publish_to_registry=false \
  -f promote_latest=false
```

普通 push/PR 和 npm release preflight 都不运行 benchmark。只有首次建立性能基线、Core/model/ORT/compiler/thread policy/runner class 变化、准备公开新的性能数字或调查疑似性能回归时，才显式触发：

```bash
gh workflow run tiled-qualification.yml --ref main -f run_benchmark=true
```

benchmark 结果是独立资格审查证据，不是每次发布的重复步骤。需要建立或更新 accepted baseline 时，仍须人工 review 并作为源码提交；脚本不会自动接受当前值。`promote_latest` 默认为 `false`，需要在 registry evidence 人工核对后显式选择。

Apple provider 的模型派生、91-function placement、tensor parity、14-fixture 质量、两 workload 性能/CPU-time、并发空缓存、cold start/RSS 和 100 次生命周期 Gate 只在真实 Apple Silicon 本机执行，不进入 GitHub Actions。标准 hosted macOS runner 是虚拟 M1，不暴露可用于资格审查的 GPU/Neural Engine；普通 CI 只保留跨平台编译、契约和轻量单测。

本地资格目录必须先用 `tools/apple/capture_identity.py` 记录真实设备身份，再依次运行 `qualify_models.py`、`quality_gate.py`、`cache_concurrency_gate.py`、`performance_gate.py` 和 `light_ocr_leak_check`。完整命令及阈值以 [Apple Device 加速技术方案](apple-device-acceleration.md) 和 `tools/apple/acceptance.json` 为准。新增真实设备报告用于把该家族加入 `validatedDeviceFamilies`，但生产 `open-macos` 不以此作为运行前置条件。`fallback_gate.py` 与 `apple_cpu_fallback` profile 只保留 D111 历史证据；D112 源码不再运行该 Gate，旧 `sessionFallback=cpu` 的拒绝由 C++/Node 契约测试覆盖。

`tools/apple/collect_qualification.py` 只从本地真机报告输出 candidate；它会验证设备身份、模型、质量、性能、缓存和生命周期报告自哈希。审阅后用 `tools/apple/accept_qualification.py` 生成并提交 `contracts/apple-provider-baselines.json`。npm release 会校验该文件的自身哈希、acceptance、模型身份与至少一个真实验证设备族，并把这些证据映射为 manifest 的 `validatedDeviceFamilies`；运行策略固定为 `devicePolicy: open-macos`，当前证据只包含 `Apple M4`。

`.github/workflows/npm-promote.yml` 只负责给已经发布且完整性已验证的 release set 更新 dist-tag。它必须引用原 `npm release` run 保存的 `light-ocr-npm-<version>` artifact，逐包复核 registry integrity，并按 model/native 依赖优先、facade 最后的顺序更新；不会重新构建、测试或发布 tarball。该 workflow 用于人工分阶段 promotion，以及 npm metadata 最终一致性导致主发布 job 在 tag 校验阶段中断后的安全恢复。

Actions 均固定到 commit SHA。D013 之前的四平台 workflow 已通过；bounded/streaming 变更后的发布候选必须重新保留每个 job 的不可变 run/artifact 证据，旧 run 不能替代当前代码。

## 9. 发布元数据

```bash
python3 tools/generate_release_metadata.py \
  --build-dir build/preset-release \
  --output-dir reports/release/macos-arm64 \
  --platform-id macos-arm64 \
  --configuration Release
```

输出：

- `build-manifest.json`：源码快照 SHA-256、Git revision（若可用）、编译器/目标/链接器/SDK/deployment target、规范化 compile commands、锁文件摘要、二进制摘要和模型归档身份。
- `license-inventory.json` 与 `licenses/`：ORT、OpenCV、zlib、Carotene、Clipper、JSON 和模型许可证/notice。
- `sbom.spdx.json`：SPDX 2.3 package/relationship/checksum 记录。

模型最终归档当前锁定为：

```text
ppocrv6-small-onnx-20260714.2.tar
bytes: 31334400
sha256: 74e246bf075c141da51e58515c731298fdabee9fd5bd8feb7cf6c7f4f352de17
```

npm release 按 [npm-packaging.md](npm-packaging.md) 生成一个 facade、一个 model 和四个 native staging packages。model package 保存上述归档的精确解包内容；发布候选另外记录六个 npm tarballs 的 bytes、SHA-256、npm integrity 和 registry identity，不能把 Core USTAR hash 当作 npm tarball hash。

## 10. 发布候选门槛

1. 在带 revision 的干净 Git snapshot 上运行全部 CI。
2. 四个 Tier 1 原生 job 全绿；不得用交叉编译替代。
3. 保存 parity、quality、benchmark、leak、Sanitizer、fuzz 和 offline 报告。
4. 将精确 bundle 文件打入 `@arcships/light-ocr-model-ppocrv6-small`，验证 sterile install，并记录 npm tarball SHA-256/integrity。独立 USTAR mirror 是非 npm 分发项，不阻塞 npm package release。
5. 为每个平台生成 manifest、许可证清单和 SBOM。
6. 在隔离环境验证六个 npm tarballs、platform 选择、默认 `createEngine()` 和模型 payload hash；已安装后的运行测试必须禁网。
7. 对照 [implementation-status.md](implementation-status.md) 关闭所有 Pending 项。

registry 发布由受保护的 `npm-release` environment 执行；workflow 成功记录、registry metadata 与安装复验才构成完成证据。签名、公证、非 npm 公共下载地址和长期保留策略仍是独立的外部事项。
