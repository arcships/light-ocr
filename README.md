# light-ocr

一个可嵌入的 C++17 PP-OCRv6 small OCR Core。输入是调用方持有的 raw pixels，输出是按阅读顺序排列的 UTF-8 text、confidence 和 quadrilateral boxes。源码树同时包含基于 Node-API v8 的异步 Node.js 适配器；当前尚未发布平台预编译 npm 包。

当前结论：C++ Core 功能、首 bundle 质量基线和 macOS arm64 本地验证已完成；Node.js 22/macOS arm64 的源码构建、真实模型识别、背压、AbortSignal 和生命周期测试已通过。四平台 Node 22/24 预编译矩阵、Linux 专属安全证据和受控模型镜像仍待外部执行。详见 [实施状态](docs/implementation-status.md)。

## 快速构建

```bash
python3 tools/bootstrap_dependencies.py --cache-dir .cache/dependencies
python3 tools/bootstrap_dependencies.py --cache-dir .cache/dependencies --offline
python3 tools/bootstrap_models.py --cache-dir .cache/models
python3 tools/package_model_bundle.py

cmake --preset release \
  -DLIGHT_OCR_DEPENDENCY_CACHE_DIR="$PWD/.cache/dependencies"
cmake --build --preset release --parallel
ctest --preset release
```

这组命令只需要 C++ 测试。完整 stage parity、质量和性能测试还需按 [构建与发布文档](docs/build-and-release.md) 安装 hash-locked Python oracle。

Node.js 适配器的本地构建、调用和测试见 [bindings/node/README.md](bindings/node/README.md)。

## 核心属性

- 官方 PP-OCRv6 small detection/recognition ONNX bytes，逐文件和最终 bundle archive 双重锁定。
- ONNX Runtime 1.22.0 CPU、OpenCV 4.10.0、Clipper 6.4.2，全部 exact archive hash。
- GRAY8、RGB8、BGR8、RGBA8；同步 API；每个 engine 同时只接收一个请求。
- checked arithmetic、图像/候选/batch/宽度/临时内存上限。
- 真实模型 integration、14-fixture stage parity、ASan/UBSan、TSan、fuzz、leak 和 performance gates。
- 10-fixture pixel-bound ground truth：文字 CER 与 detection precision/recall/Hmean 均可重复生成报告。
- Core 运行时不联网、不启动进程、不读取隐式模型路径，也不依赖 Python。

## 文档

- [需求](docs/requirements.md)
- [实施与验收状态](docs/implementation-status.md)
- [架构](docs/architecture.md)
- [C++ API](docs/native-api.md)
- [Node-API 使用与构建](bindings/node/README.md)
- [Node-API 设计](docs/napi-design.md)
- [模型 bundle](docs/model-bundle.md)
- [对齐与质量](docs/parity-testing.md)
- [构建与发布](docs/build-and-release.md)
- [决策记录](docs/decisions.md)

公共 API 只有 [core.hpp](include/light_ocr/core.hpp)、[types.hpp](include/light_ocr/types.hpp) 和 [error.hpp](include/light_ocr/error.hpp)。当前不承诺稳定 ABI 或外部 SDK 安装布局。
