# light-ocr Node-API adapter

状态：v1 adapter 与 `@arcships/light-ocr@0.1.0` 已发布；`main` 上的 0.2.0 candidate 新增 tiled detection 和内存 JPEG/PNG 输入，尚未发布。0.1.0 的 macOS arm64/x64、Linux x64 glibc、Windows x64 Node.js 22/24 package matrix、真实 PP-OCRv6 和禁网运行均已通过。

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
- 产品 engine 默认报告 `detectionStrategy: 'bounded'`、`detectionMaxSide: 960` 和 `defaultRecognitionBatchSize: 1`。0.2.0 candidate 可通过 `detection: {strategy: 'tiled'}` 显式选择 `tiled-v1`；`upstreamExact` 只用于上游对照，单次 `recognize({detectionMaxSide})` 只能继续降低 bounded engine 的 side。

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

macOS/Linux 产物在 `build-node/bin/light_ocr_node.node`，锁定的 ONNX Runtime 动态库会放在同一目录。Windows 还需通过 `LIGHT_OCR_NODE_LIBRARY` 指定当前架构的 `node.lib`。

开发期用 `LIGHT_OCR_NODE_BINARY` 指向刚构建的 addon：

```bash
export LIGHT_OCR_NODE_BINARY="$PWD/build-node/bin/light_ocr_node.node"
export LIGHT_OCR_MODEL_BUNDLE="$PWD/models/generated/ppocrv6-small-onnx-20260714.2"

node --test --test-concurrency=1 bindings/node/test/adapter.test.cjs
# 或：ctest --test-dir build-node -R '^light_ocr_node_tests$' --output-on-failure
```

当前源码加载器不会搜索任意 cwd。开发构建优先接受 `LIGHT_OCR_NODE_BINARY`，发布构建则按 `process.platform`、`process.arch` 和 Linux libc 固定选择四个 `@arcships/light-ocr-<platform>` optional packages。facade 通过 model package 导出的 manifest 定位默认 bundle，并在进入 native addon 前核对 bundle ID。详见 [npm package 设计](../../docs/npm-packaging.md)。

## 使用

发布后的目标用法不需要模型路径：

```js
const { createEngine, OcrError } = require('@arcships/light-ocr');

const engine = await createEngine({ queueCapacity: 4 });
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
