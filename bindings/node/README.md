# light-ocr Node-API adapter

状态：`@arcships/light-ocr@0.3.0` 已发布并提升为 npm `latest`。默认 `createEngine()` 使用 descriptor-driven Auto；macOS arm64 可用 Apple/Core ML，Linux x64 glibc/Vulkan 与 Windows x64/D3D12 可用 official Native WebGPU Plugin EP，macOS x64 保持 CPU-only。

推荐直接安装公开 package：

```bash
npm install @arcships/light-ocr
```

安装会带上 PP-OCRv6 Small 模型与当前平台的预编译 native package；`createEngine()` 默认无需 `bundlePath`。以下本地构建说明用于修改或调试 adapter。

## 能力边界

- 原始 Node-API C API，编译为 `NAPI_VERSION=8`，不依赖 `node-addon-api`。
- `createEngine()`、`recognize()`、`close()` 全部返回 Promise。
- 每个 engine 一条专用 C++ worker thread 和一个有界 FIFO；推理不占 JavaScript 线程或 libuv 共享线程池。
- `recognize()` 接受 `Uint8Array` raw pixels：`gray8`、`rgb8`、`bgr8`、`rgba8`。
- `recognizeEncoded()` 接受内存中的 JPEG/PNG `Uint8Array`；格式自动检测，解码在 engine worker 中执行。
- `recognize()` 返回前同步复制本次调用实际需要的像素范围；调用返回后可以立即修改或复用原 Buffer。
- 支持 `AbortSignal` 协作式取消：queued 请求会从队列移除；running 请求立即拒绝 public Promise，但 Core 会安全运行到返回并丢弃结果。
- native addon 只接收现有绝对 bundle 目录。当前源码开发调用显式传 `bundlePath`；发布后的 facade 默认使用随 npm 安装的 model package 路径。
- 产品 engine 默认报告 `detectionStrategy: 'bounded'`、`detectionMaxSide: 960` 和 `defaultRecognitionBatchSize: 1`。0.3.0 可通过 `detection: {strategy: 'tiled'}` 显式选择 `tiled-v1`；`upstreamExact` 只用于上游对照，单次 `recognize({detectionMaxSide})` 只能继续降低 bounded engine 的 side。
- `createEngine({execution})` 接受 `auto`、`cpu`、`apple` 与已交付平台支持的 `webgpu`。macOS 15+ arm64 默认开放：Apple Silicon interactive 使用 FP16 ANE + 宽文本 FP16 GPU，strict 使用全 GPU；macOS x64 package 保持 CPU-only。WebGPU 使用 ORT Core 1.24.4 + official plugin 0.1.0，Linux 为 Vulkan、Windows 为 D3D12；`0.3.0` 公共 WebGPU profile 只接受 `precision: 'auto' | 'fp32'`，FP16 仅用于 Apple provider。当前 WebGPU 模型需要 `Concat/Gather/Slice` 三类有界 CPU partition，因而 `cpuPartition: 'forbid'` 会稳定 fail-closed。只有 Auto 可在创建期按 descriptor 锁定的 typed failure 继续候选；显式 provider 不回退，旧 `sessionFallback: 'cpu'` 返回 `invalid_argument`。`engine.info.execution.sessions` 报告每个模型的实际 provider chain、precision、adapter、runtime/provider/qualification identity，selection trace 则报告 Auto 的每次创建尝试。

## `0.3.0` 加速证据

| Provider 与记录设备 | P50 结果 | 质量与 Gate |
| --- | ---: | --- |
| Apple/Core ML，Apple M4 Max | `HELLO 123` 2.30×；XFUND 2.85× | 14 fixtures 通过 CPU parity 阈值 |
| WebGPU/Vulkan，NVIDIA RTX 5060 Ti | 14-fixture 聚合 5.70×；单项 3.47×–9.30× | 14/14 与 CPU FP32 一致；164/164 |
| WebGPU/D3D12，AMD Radeon 780M | 14-fixture 聚合 2.44×；单项 1.28×–2.98× | 14/14 与 CPU FP32 一致；164/164 |

这些结果都是表中设备上的同机 CPU 对照。WebGPU 聚合值为 14 个 fixture 的 CPU P50 总和除以 WebGPU P50 总和，不外推到其他 GPU/driver。

不支持 WebP、GIF、PDF、EXIF orientation 自动旋转、zero-copy/transfer、运行中 inference 硬中断、Electron 或 Bun。详细契约见 [Node-API 设计](../../docs/napi-design.md)。

## 本地构建

先按仓库根目录文档准备锁定的依赖缓存和 PP-OCRv6 bundle。Node headers 必须显式传给 CMake：

```bash
NODE_INCLUDE_DIR="$(node -p \
  "require('node:path').resolve(require('node:path').dirname(process.execPath), '../include/node')")"

cmake -S . -B build-node \
  -DCMAKE_BUILD_TYPE=Debug \
  -DLIGHT_OCR_DEPENDENCY_CACHE_DIR="$PWD/.cache/dependencies" \
  -DLIGHT_OCR_BUILD_NODE=ON \
  -DLIGHT_OCR_BUILD_TESTS=ON \
  -DLIGHT_OCR_BUILD_TOOLS=OFF \
  -DLIGHT_OCR_NODE_INCLUDE_DIR="$NODE_INCLUDE_DIR" \
  -DLIGHT_OCR_NODE_EXECUTABLE="$(command -v node)"

cmake --build build-node --target light_ocr_node --parallel
```

macOS/Linux 的链接产物在 `build-node/bin/light_ocr_node.node`；可加载的完整开发 runtime 会连同 descriptor 与锁定的 ONNX Runtime 一起放在 `build-node/node-runtime/native/`。Windows 还需通过 `LIGHT_OCR_NODE_LIBRARY` 指定当前架构的 `node.lib`，staged runtime 位于 `build-node/node-runtime/Release/native/`。

WebGPU 构建必须先用 [`tools/webgpu/build_runtime.py`](../../tools/webgpu/README.md) 生成并验证目标 SDK，再增加：

```bash
cmake -S . -B build-node-webgpu -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLIGHT_OCR_DEPENDENCY_CACHE_DIR="$PWD/.cache/dependencies" \
  -DLIGHT_OCR_ONNXRUNTIME_FLAVOR=webgpu \
  -DLIGHT_OCR_WEBGPU_SDK_DIR="$PWD/dist/webgpu-sdk/linux-x64" \
  -DLIGHT_OCR_WEBGPU_QUALIFICATION_BUILD=ON \
  -DLIGHT_OCR_BUILD_NODE=ON \
  -DLIGHT_OCR_NODE_INCLUDE_DIR="$NODE_INCLUDE_DIR" \
  -DLIGHT_OCR_NODE_EXECUTABLE="$(command -v node)"
```

qualification staged payload 在 Linux 包含 core + plugin，在 Windows 还包含 `dxcompiler.dll` 与 `dxil.dll`。schema 2 descriptor 精确声明所有 native 文件及 SHA-256；loader 从 sterile cwd 验证完整 inventory 后才加载 addon，Core 在注册 plugin 前再次复核 provider library。普通 release 构建不接受 pending qualification lock。

开发期用 `LIGHT_OCR_NODE_BINARY` 指向刚构建的 addon：

```bash
export LIGHT_OCR_NODE_BINARY="$PWD/build-node/node-runtime/native/light_ocr_node.node"
export LIGHT_OCR_RUNTIME_DESCRIPTOR="$PWD/build-node/node-runtime/native/runtime-descriptor.json"
export LIGHT_OCR_MODEL_BUNDLE="$PWD/models/generated/ppocrv6-small-webgpu-20260719.1"
export LIGHT_OCR_APPLE_MODEL_BUNDLE="$PWD/models/generated/ppocrv6-small-native-20260719.1"

node --test --test-concurrency=1 bindings/node/test/adapter.test.cjs
# 或：ctest --test-dir build-node -R '^light_ocr_node_tests$' --output-on-failure
```

当前源码加载器不会搜索任意 cwd。开发构建优先接受 `LIGHT_OCR_NODE_BINARY`，发布构建则按 `process.platform`、`process.arch` 和 Linux libc 固定选择四个 `@arcships/light-ocr-<platform>` optional packages。facade 通过 model package 导出的 manifest 定位默认 bundle，并在进入 native addon 前核对 bundle ID。详见 [npm package 设计](../../docs/npm-packaging.md)。

## 使用

发布后的目标用法不需要模型路径：

```js
const { createEngine, OcrError } = require('@arcships/light-ocr');

const engine = await createEngine({
  queueCapacity: 4,
  execution: {
    provider: 'webgpu',
    precision: 'fp32',
    cpuPartition: 'allow',
    sessionFallback: 'error',
  },
});

console.log(engine.info.execution.sessions.detection.actualProviderChain);
console.log(engine.info.execution.sessions.detection.deviceValidated);
```

当前源码开发用法仍需显式 bundle：

CommonJS：

```js
const { createEngine, OcrError } = require('./bindings/node/js/index.cjs');

async function main() {
  const engine = await createEngine({
    bundlePath: '/absolute/path/to/ppocrv6-small-bundle',
    queueCapacity: 4,
    maxPendingInputBytes: 256 * 1024 * 1024,
  });

  try {
    const controller = new AbortController();
    const result = await engine.recognize(
      {
        data: bgrPixels,
        width,
        height,
        stride,
        pixelFormat: 'bgr8',
      },
      {
        includeDiagnostics: true,
        signal: controller.signal,
      },
    );
    const encodedResult = await engine.recognizeEncoded(
      await require('node:fs/promises').readFile('/absolute/path/to/image.jpg'),
      { signal: controller.signal },
    );
    console.log(result.lines);
    console.log(encodedResult.lines);
  } catch (error) {
    if (error instanceof OcrError) console.error(error.code, error.message, error.detail);
    else throw error; // 包括调用方提供的 AbortSignal.reason
  } finally {
    await engine.close();
  }
}

main().catch(console.error);
```

发布包的 ESM 使用 `import { createEngine, OcrError } from '@arcships/light-ocr'`；当前源码直接加载 `./bindings/node/js/index.mjs`。完整 TypeScript 类型位于 [index.d.ts](js/index.d.ts)，目标 package API 见 [Node-API 设计](../../docs/napi-design.md)。

## 背压与取消

`queueCapacity` 统计同一 engine 的 running + queued 请求；`maxPendingInputBytes` 统计尚未被 Core 消费完的输入快照。任一临时不足都会以 `OcrError`/`queue_full` 拒绝，不会隐式等待或无界增长。单个输入自身超过 byte budget 时返回 `resource_limit_exceeded`。

AbortSignal 的 public Promise 以最先观察到的终态为准：

- 调用前已 aborted：不 admission，不复制 pixels，按原始 `signal.reason` 拒绝。
- queued：立即释放 request slot、snapshot 和 byte budget，永不进入 Core。
- running：立即按 reason 拒绝；底层 inference 继续，engine 在它返回前仍占用执行槽。

这不是硬超时。需要强制终止不可信或严格 deadline 的任务时，应使用独立进程作为隔离边界。

## 生命周期

`close()` 幂等，并按 FIFO drain 已接收的请求；close 开始后不再接收新请求。建议始终在 `finally` 中 await close。忘记 close 时，wrapper finalizer 只发出非阻塞关闭请求；Node Environment teardown 会停止 completion transport、丢弃未开始请求并等待正在运行的 Core 调用安全结束。

每个 `worker_threads` Environment 有独立 dispatcher、engine registry 和 request ID 空间；engine 对象不能跨 Environment 传递。

## Sanitizer 注意事项

addon 和它所链接的 Core 可以用项目的 ASan/UBSan 或 TSan 选项编译，但测试进程也必须是与该 sanitizer 兼容的 Node host。macOS 官方签名 Node 可能清除 `DYLD_INSERT_LIBRARIES`，从而以“interceptors loaded too late”在加载 addon 前退出；release gate 应使用专门的 instrumented Node/runner，不能把 Core executable 的 sanitizer 启动命令原样套给 `.node`。
