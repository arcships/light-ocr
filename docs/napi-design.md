# light-ocr Node-API 适配器设计

状态：v1 adapter 与 `@arcships/light-ocr@0.1.0` 已发布；0.2.0 tiled mapping 与内存 JPEG/PNG 输入已在源码实现，尚未发布<br>
更新时间：2026-07-14  
Authority：JavaScript/TypeScript API、异步调度、内存所有权、Node.js 生命周期与 npm 布局  
Core contract：[native-api.md](native-api.md)  
Decision：[decisions.md](decisions.md) D101、D105

`DetectionStrategy: "tiled"` 的 additive Node types、diagnostics 和 runtime identity，以及 `recognizeEncoded()` JPEG/PNG 内存输入均已进入 0.2.0 candidate 源码。tiled 算法与 lockstep 发布条件见 [Tiled Detection 技术设计与验收规格](tiled-design-and-acceptance.md)。这些能力不属于已发布的 `0.1.0` API，完整门槛通过前也不会进入 npm `latest`。

## 1. 结论

第一版 Node.js 适配器是 C++ Core 的异步、无损映射：

- 使用原始 Node-API C 接口，编译目标固定为 `NAPI_VERSION=8`。
- 支持 Node.js 22 和 24；Node.js 26 先做前向兼容 smoke test，不在其进入 LTS 并通过完整矩阵前声明正式支持。
- 每个 JavaScript `OcrEngine` 拥有一个 C++ Core `Engine` 和一条专用原生工作线程。
- 每个 engine 使用有界 FIFO 队列；不同 engine 可以并行，同一 engine 永不并发进入 Core。
- `createEngine`、`recognize` 和 `close` 都返回 Promise，OCR 推理不占用 JavaScript 线程，也不占用 Node.js/libuv 共享工作池。
- `recognize` 从 v1 起接受 `AbortSignal`：queued 请求真正取消，running 请求立即停止向调用方交付结果，但 native inference 安全运行到结束。
- `recognize` 返回前复制调用所需的 raw pixel 字节。调用返回后，调用方可以立即复用或修改原 Buffer。
- 发布后的 `@arcships/light-ocr` 默认使用随 npm dependency 安装的 PP-OCRv6 small bundle；调用方无需另行下载或配置模型。
- JS facade 把内置模型解析为绝对 bundle 目录后交给 native 层。engine 创建和识别不联网、不下载模型、不启动进程、不依赖 cwd 或环境变量。
- Core 的错误码、几何、置信度、固定阶段 timing 和 diagnostics 原义保留。

模型与平台二进制的 npm 拆包、版本和发布契约见 [npm-packaging.md](npm-packaging.md)。Core 和 native addon 仍只理解本地 bundle 目录；npm package 解析由 JS facade 负责，不把 registry 或模型发现逻辑下沉到 C++。

## 2. 目标与非目标

### 2.1 v1 目标

1. 给 Node.js 提供类型完整、Promise 化的 detection + recognition API。
2. 不阻塞 JavaScript 线程执行 OCR，不挤占 libuv 的共享 worker pool。
3. 明确定义输入快照、队列背压、关闭、GC、`worker_threads` 和环境退出行为。
4. 与 C++ Core 做字段级、错误级和结果级 parity。
5. 复用 Core 的模型验证、资源限制和 OCR 算法，不在适配器复制业务逻辑。
6. 为四个 Core Tier 1 目标提供 Node-API 预编译包。
7. `npm install @arcships/light-ocr` 同时取得默认模型，正常调用 `createEngine()` 不需要 `bundlePath`。

### 2.2 v1 非目标

- WebP、GIF、PDF 等输入解码；JPEG/PNG 由受限的内存 decoder 支持。
- install/postinstall 或运行时网络下载、默认目录扫描或模型自动更新。
- 无模型瘦包、按语言拆分模型、tiny/medium/orientation 模型。
- 对运行中的 ONNX Runtime inference 做硬中断或强制超时终止。
- GPU/Metal/CUDA/DirectML Execution Provider。
- Electron、Bun、Deno 或浏览器支持声明。
- Linux musl、Linux arm64、Windows arm64。
- 跨进程共享 engine、跨 Node.js Environment 传递 engine。
- 稳定 C ABI 或通用 C++ SDK 安装布局。

以上项目若加入，必须是独立、可测试的增量，不得改变既有 Core OCR 语义。

## 3. 公共 TypeScript API

公开 import 名称固定为 `@arcships/light-ocr`。

```ts
export type PixelFormat = "gray8" | "rgb8" | "bgr8" | "rgba8";
export type BuiltInModel = "ppocrv6-small";

export interface RawImage {
  readonly data: Uint8Array;
  readonly width: number;
  readonly height: number;
  readonly stride: number;
  readonly pixelFormat: PixelFormat;
}

export interface ResourceLimits {
  readonly maxWidth: number;
  readonly maxHeight: number;
  readonly maxPixels: number;
  readonly maxDetectionSide: number;
  readonly maxDetectionCandidates: number;
  readonly maxDetectionTiles: number;
  readonly maxRecognitionBatchSize: number;
  readonly maxRecognitionWidth: number;
  readonly maxTemporaryBytes: number;
}

export type DetectionStrategy = "bounded" | "tiled" | "upstreamExact";

export interface DetectionOptions {
  readonly strategy?: DetectionStrategy;
  readonly maxSide?: number;
}

export interface CreateEngineOptions {
  /** Built-in package model. Defaults to ppocrv6-small. */
  readonly model?: BuiltInModel;
  /** Advanced override: existing absolute directory containing one complete bundle. */
  readonly bundlePath?: string;
  readonly intraOpThreads?: number;
  readonly interOpThreads?: number;
  readonly recognitionScoreThreshold?: number;
  readonly recognitionBatchSize?: number;
  readonly detection?: DetectionOptions;
  /** Complete replacement; every value may only reduce the bundle ceiling. */
  readonly reducedLimits?: Omit<ResourceLimits, "maxDetectionTiles"> & {
    /** Omission preserves the 0.1 reducedLimits source shape. */
    readonly maxDetectionTiles?: number;
  };
  /** Running plus queued recognize calls. Default 4; range 1..64. */
  readonly queueCapacity?: number;
  /** Pixel snapshots retained by this engine. Default 256 MiB; maximum 1 GiB. */
  readonly maxPendingInputBytes?: number;
}

export interface RecognizeOptions {
  readonly recognitionScoreThreshold?: number;
  readonly recognitionBatchSize?: number;
  readonly includeDiagnostics?: boolean;
  readonly signal?: AbortSignal;
  /** The initial bundle reports this capability as false. */
  readonly useTextlineOrientation?: boolean;
  /** May only lower the side of a bounded engine; tiled rejects this field. */
  readonly detectionMaxSide?: number;
}

export interface Point {
  readonly x: number;
  readonly y: number;
}

export interface OcrLine {
  readonly text: string;
  readonly confidence: number;
  readonly box: readonly [Point, Point, Point, Point];
}

export type RejectionReason = "below_score_threshold" | "empty_decode";

export interface RejectedLine {
  readonly line: OcrLine;
  readonly reason: RejectionReason;
}

export interface DiagnosticWarning {
  readonly code: string;
  readonly message: string;
}

export interface Diagnostics {
  readonly rejectedLines: readonly RejectedLine[];
  readonly warnings: readonly DiagnosticWarning[];
  readonly detectedCandidates: number;
  readonly acceptedBoxes: number;
  readonly detectionInputWidth: number;
  readonly detectionInputHeight: number;
  readonly rawDetectionBoxes: number;
  readonly suppressedDuplicateBoxes: number;
  readonly maxLiveDetectionPassBuffers: number;
  readonly detectionPasses: readonly {
    readonly tileOrdinal: number;
    readonly x: number;
    readonly y: number;
    readonly width: number;
    readonly height: number;
    readonly tensorWidth: number;
    readonly tensorHeight: number;
    readonly contourCandidates: number;
    readonly rawCandidates: number;
  }[];
  readonly recognitionBatchShapes: readonly {
    readonly batchSize: number;
    readonly height: number;
    readonly width: number;
  }[];
}

export interface TimingUs {
  readonly total: number;
  readonly decode: number;
  readonly inputValidation: number;
  readonly detectionPreprocess: number;
  readonly detectionInference: number;
  readonly detectionPostprocess: number;
  readonly detectionMerge: number;
  readonly cropAndSort: number;
  readonly recognitionPreprocess: number;
  readonly recognitionInference: number;
  readonly recognitionPostprocess: number;
}

export interface OcrResult {
  readonly lines: readonly OcrLine[];
  readonly imageWidth: number;
  readonly imageHeight: number;
  readonly modelBundleId: string;
  readonly timingUs: TimingUs;
  /** Absent unless includeDiagnostics is true. */
  readonly diagnostics?: Diagnostics;
}

export interface EngineInfo {
  readonly coreVersion: string;
  readonly modelBundleId: string;
  readonly modelBundleSchemaVersion: string;
  readonly normalizedConfigSchemaVersion: string;
  readonly backend: string;
  readonly executionProvider: string;
  readonly capabilities: {
    readonly detection: boolean;
    readonly recognition: boolean;
    readonly textlineOrientation: boolean;
    readonly tiledDetection: boolean;
  };
  /** Reports the underlying Core contract unchanged. */
  readonly concurrencyMode: "serialized_reject_when_busy";
  readonly limits: ResourceLimits & { readonly maxConcurrentCalls: 1 };
  readonly intraOpThreads: number;
  readonly interOpThreads: number;
  readonly detectionStrategy: DetectionStrategy;
  readonly detectionMaxSide: number;
  readonly tiledDetection?: {
    readonly contractVersion: "tiled-v1";
    readonly tileSide: 1280;
    readonly minimumOverlap: 128;
    readonly artificialBoundaryMargin: 32;
    readonly mergeIouThreshold: 0.5;
    readonly mergeIosThreshold: 0.8;
  };
  readonly defaultRecognitionScoreThreshold: number;
  readonly defaultRecognitionBatchSize: number;
  readonly adapter: {
    readonly scheduler: "dedicated_fifo";
    readonly queueCapacity: number;
    readonly maxPendingInputBytes: number;
  };
}

export type CoreErrorCode =
  | "invalid_argument"
  | "invalid_image"
  | "unsupported_pixel_format"
  | "unsupported_capability"
  | "invalid_model_bundle"
  | "unsupported_model"
  | "model_integrity_failed"
  | "runtime_initialization_failed"
  | "inference_failed"
  | "postprocess_failed"
  | "resource_limit_exceeded"
  | "invalid_engine"
  | "internal_error";

export type AdapterErrorCode =
  | "bundle_io_failed"
  | "queue_full"
  | "environment_closing"
  | "unsupported_platform"
  | "package_load_failed";

export type OcrErrorCode = CoreErrorCode | AdapterErrorCode;

export class OcrError extends Error {
  constructor(code: OcrErrorCode, message: string, detail?: string);
  readonly name: "OcrError";
  readonly code: OcrErrorCode;
  readonly detail?: string;
}

export interface OcrEngine {
  /** Deep-frozen snapshot created after native engine initialization. */
  readonly info: EngineInfo;
  recognize(image: RawImage, options?: RecognizeOptions): Promise<OcrResult>;
  recognizeEncoded(data: Uint8Array, options?: RecognizeOptions): Promise<OcrResult>;
  /** Idempotent: stop admission, drain accepted work, release native state. */
  close(): Promise<void>;
}

export function createEngine(options?: CreateEngineOptions): Promise<OcrEngine>;
```

`Buffer` 是 `Uint8Array` 的子类，因此可以直接作为 `RawImage.data` 或 `recognizeEncoded()` 输入。不接受 `DataView`、其他 TypedArray 或以 `SharedArrayBuffer` 为 backing store 的 `Uint8Array`。

`OcrEngine` 没有 public constructor，只能由成功的 `createEngine` 创建。未传 `model`/`bundlePath` 时默认使用内置 `ppocrv6-small`；二者同时出现是 `invalid_argument`。`reducedLimits` 一旦提供就必须包含全部八个字段；适配器把 Core 固定的 `maxConcurrentCalls=1` 补入 native options。所有配置对象拒绝未知 own property，避免拼写错误被静默忽略。预期的参数、package、I/O、Core 和队列错误都通过 Promise rejection 返回 `OcrError`；取消按 `AbortSignal.reason` 拒绝，默认 `AbortController.abort()` 因而得到标准 `AbortError`。只有非法 receiver、Node-API 无法创建 Promise 或不可恢复的运行时故障可能同步抛出。

### 3.1 使用示例

```ts
import { createEngine } from "@arcships/light-ocr";

const engine = await createEngine({
  queueCapacity: 4,
});

try {
  const result = await engine.recognize({
    data: rgba,
    width,
    height,
    stride: width * 4,
    pixelFormat: "rgba8",
  });

  for (const line of result.lines) {
    console.log(line.text, line.confidence, line.box);
  }
} finally {
  await engine.close();
}
```

## 4. 字段映射规则

JavaScript 使用 camelCase，C++ 使用 snake_case；除命名外不改变值或语义。

| C++ Core | JavaScript | 规则 |
| --- | --- | --- |
| `PixelFormat::gray8` 等 | `"gray8"` 等 | 未知值拒绝，不做默认降级 |
| `Quad.points[4]` | `box: [Point, Point, Point, Point]` | 保留顺序和浮点坐标 |
| `OcrLine::confidence` | `confidence` | 不重标定、不四舍五入 |
| `Timing::*_us` | `timingUs.*` | 保持微秒单位 |
| adapter decode duration | `timingUs.decode` | raw 为零；encoded 时计入 `total` |
| `optional<Diagnostics>` | `diagnostics?` | 未请求时属性缺失 |
| `ErrorCode` | `OcrError.code` | Core 字符串逐字保持 |
| `Error::detail` | `OcrError.detail` | 空字符串映射为属性缺失 |

Core timing 的 `uint64_t` 映射为 JavaScript `number`。转换前必须检查不超过 `Number.MAX_SAFE_INTEGER`；微秒计时达到该边界需要约 285 年，正常调用不会触发。越界按 `internal_error` 处理，不能静默丢精度。

`OcrResult` 是调用方拥有的普通 JavaScript 数据，native 层在 Promise settled 后不再持有它。`readonly` 是 TypeScript 契约，不强制冻结每份结果；`engine.info` 因为是稳定元数据，会在创建时 deep-freeze。

## 5. Bundle 输入与文件安全

JS facade 先确定 effective bundle path：默认或 `model: "ppocrv6-small"` 从必需的 `@arcships/light-ocr-model-ppocrv6-small` dependency 取得只读绝对路径；显式 `bundlePath` 则必须是当前平台的绝对目录路径。适配器不扫描默认目录，不把相对路径转换为绝对路径，也不读取 cwd 或环境变量寻找模型。`createEngine` 的 Promise 只在下列步骤全部成功后 resolve：

1. 安全打开 bundle 根目录。
2. 枚举并读取完整文件集合。
3. 调用 `ModelBundle::create` 验证 schema、inventory、hash、模型契约和资源上限。
4. 调用 `Engine::create` 创建 detection/recognition ONNX Runtime session。
5. 在 JavaScript 线程构造、type-tag 并返回 `OcrEngine`。

不能直接把 `tools/common/bundle_files.cpp` 当作生产 loader。N-API loader 必须：

- 根目录和所有子项都不跟随 symlink/reparse point。
- 打开文件句柄后再次确认它是 regular file，并从该句柄读取，避免 check/open 间替换。
- 最多读取 64 个文件；单文件最多 256 MiB；总字节最多 512 MiB，与 Core 前置上限一致。
- 使用 checked arithmetic；在分配前核对文件声明大小和累计大小。
- 生成相对、正斜线分隔的 normalized path；拒绝空段、`.`、`..`、绝对成员路径、NUL 和重复路径。
- 短读、读取中变化、权限错误、目录缺失或非目录返回 `bundle_io_failed`。
- 将 immutable shared byte vectors 交给 `ModelBundle::create`；最终是否可信仍由 Core hash 验证决定。

POSIX 实现使用 directory-relative、no-follow 的打开方式；Windows 使用 handle-based traversal 并拒绝 reparse point。测试必须包含 symlink/reparse point、TOCTOU 替换、深层目录、超限文件、重复和读到一半被截断。

适配器不会读取 `.tar`/`.zip`，也不会联网补文件。model package 直接携带解包后的 bundle 目录；安装和 registry 下载发生在运行进程之外。

## 6. 输入验证与所有权

### 6.1 同步边界工作

`recognize()` 在 JavaScript 线程完成以下有限工作，然后立即返回 Promise：

1. 校验 receiver、对象 shape、所有整数为 finite safe integer、score threshold 为 `[0, 1]` 内 finite number、boolean 不做 truthy coercion，并校验 pixel format/options/AbortSignal。若 signal 已 aborted，直接按 `signal.reason` 拒绝，不分配 snapshot 或占用队列。
2. 要求 width/height 在 `1..UINT32_MAX`、stride 为正且可表示为 `size_t`；先按 effective engine limits 校验 dimensions/pixels，再根据 format 计算 `rowBytes = width * channels`。零值或不可表示 metadata 是 `invalid_image`，可表示但超过 limits 是 `resource_limit_exceeded`。
3. 使用 checked arithmetic 计算最小可读范围：`requiredBytes = (height - 1) * stride + rowBytes`。
4. 通过 `napi_get_typedarray_info` 确认类型恰为 `napi_uint8_array`，再要求 backing value 通过 `napi_is_arraybuffer`；由此排除 SharedArrayBuffer。随后校验 ArrayBuffer 未 detached、`stride >= rowBytes` 且 view 长度足够。
5. 原子预留一个队列名额和 `requiredBytes` pending-input budget。
6. 分配 native vector，并复制恰好 `requiredBytes`；不复制 view 末尾无关字节。
7. 将只含 native 数据的请求入队。

`recognizeEncoded()` 复用相同 options、admission、snapshot、AbortSignal 和 completion 语义，但 JavaScript 线程只校验 encoded view/backing store、非空输入并复制 bytes，不同步解析图片内容。worker 使用关闭 stdio 且只启用 JPEG/PNG 的 `stb_image` 自动检测格式，先读取 dimensions 并按 effective `maxWidth`、`maxHeight`、`maxPixels` 和 `maxTemporaryBytes` 拒绝超限输入，再解码为 RGB8 后进入不变的 Core raw-pixel API。请求级 allocator 统计并限制 stb 的 `malloc/realloc/free`，同时在复制 RGB 输出前计入重叠存活的 decoder buffer 和 native vector；allocator 拒绝或系统分配失败均映射为 `resource_limit_exceeded`。EXIF orientation 不自动应用。`timingUs.decode` 记录 worker 解码时间，并计入 `timingUs.total`；raw input 的 `decode` 固定为零。

native admission 返回一个不导出的 `{ requestId, promise }`。JS facade 在 public Promise settled 前监听一次 signal；abort 时调用 private native cancel，并立即按 `signal.reason` reject public Promise。native promise 始终安装 fulfillment/rejection handler，所以取消后晚到的内部 completion 不会形成 unhandled rejection。listener 在 success、error 或 abort 任一路径只移除一次。

必须先预留、后复制。队列或字节预算不足时直接以 `queue_full` 拒绝，不能先复制大图再发现背压。分配或复制失败会释放预留并拒绝 Promise。

raw input 的错误优先级固定为：environment/engine state，JavaScript 结构与类型，capability/recognition options，image metadata 与 limits，adapter admission，最后是 worker 中的 Core/runtime error。因而 raw input 的 malformed metadata 不会被当成 `queue_full`，unsupported orientation 也不会为了排队而复制 pixels。

encoded input 在 admission 前只验证 JavaScript 类型、backing store、detached/empty 状态和 byte budget；格式、dimensions、pixels 及 decoder 内存限制在 worker 中验证。因此 engine 已满时，malformed 或 dimension 超限的 encoded payload 可以先返回 `queue_full`。这是避免在 JavaScript 线程同步执行重复图片解析的明确 API 契约。

普通 `ArrayBuffer` 在同步 native 调用期间不能由同一 JavaScript Agent 并发执行修改；完成快照后不再访问 V8 backing store。拒绝 `SharedArrayBuffer` 是为了避免另一 Agent 在复制期间写入导致不一致快照或 native data race。这里不依赖较新、experimental 的 `node_api_is_sharedarraybuffer`，因此仍只使用 Node-API v8 symbols。

### 6.2 为什么 v1 不做 zero-copy

仅用 `napi_ref` 保留 Buffer 只能保证 backing store 存活，不能阻止调用方修改字节。让工作线程直接读取它会使结果依赖调用后的突变，也可能引入数据竞争。v1 因此选择确定性的 snapshot 语义。

快照会产生与输入字节数成正比的同步调用成本。benchmark 必须单独报告 snapshot 时间；未来可设计显式 transfer/immutable input API，但不得把不安全 zero-copy 偷换成默认行为。

### 6.3 内存预算

每个 engine 同时满足两个 admission 条件：

- `running + queued recognize <= queueCapacity`；默认 4，合法范围 1..64。
- 所有未完成请求的 raw-pixel 或 encoded-byte snapshot 总和 `<= maxPendingInputBytes`；默认 256 MiB，硬上限 1 GiB。encoded 解码结果只在串行 worker 当前请求中存活，并额外受 `maxTemporaryBytes` 约束。

单个 snapshot 大于该 engine 的 `maxPendingInputBytes` 返回 `resource_limit_exceeded`；预算本身足够、但当前被其他请求占用时返回 `queue_full`。Core 的 `maxTemporaryBytes` 继续独立限制推理过程临时内存。

计数释放点固定如下：Core `recognize` 返回后，worker 立即销毁 input snapshot 并归还 pending-input bytes；请求 count 要等 completion payload 成功进入 dispatcher（或在 teardown 中被明确丢弃）后才归还。Promise liveness count 则只在 JavaScript 线程 settle 对应 Promise 后归还。这样 transport backpressure 不会让同一 engine 无限制继续产出结果。

native 持有的 bundle 和 snapshot 字节通过 `napi_adjust_external_memory` 在 JavaScript 主线程记账；硬限制仍由上面的显式计数保证，不能依赖 V8 GC 决定安全性。

## 7. 调度模型

### 7.1 每个 engine 一条专用线程

每个 `EngineState` 包含：

- 状态：`loading -> open -> closing -> closed`，失败初始化进入 `failed`。
- 一个 `std::thread`、mutex、condition variable 和 FIFO deque。
- 一个且仅一个 `std::unique_ptr<light_ocr::Engine>`，只在该线程访问。
- `queueCapacity`、pending count 和 pending-input byte count。
- 已接受 Promise 的完成记录；其中的 `napi_deferred` 只在 JavaScript 线程使用。
- 单调递增的 request ID，以及 `queued -> running -> completion_queued -> settled` 状态；取消可以把 `queued` 变为 `cancelled`，或把 `running` 标记为 `discard_result`。

初始化任务也是该线程的第一个任务，所以文件读取、bundle hash、ONNX session 创建都不阻塞 JavaScript。一个 engine 的工作线程按 FIFO 调用同步 Core；因此永远不会触发 Core 的同 engine busy 分支。多个 engine 各有独立线程和 Core session，可以真正并行。

初始化失败时不暴露半成品 wrapper：worker 提交 error completion 后退出，JavaScript completion callback 先 reject `createEngine` Promise，再 join worker 并释放状态。`std::thread` 创建失败也转换为 rejected Promise，且必须在返回前撤销 dispatcher acquire 和 engine registry entry。

默认 `intraOpThreads=1`、`interOpThreads=1`。创建很多 engine 会同时创建 engine worker 和 ONNX Runtime 线程；这是显式容量选择，不由适配器建立隐藏的全局 engine pool。

### 7.2 不使用 `napi_async_work`

OCR 是长时间、CPU 密集型任务。`napi_async_work` 使用 Node.js/libuv 共享工作池，可能与 filesystem、crypto、DNS 等宿主任务争用，并且不能表达本设计的 per-engine FIFO 和双重背压。适配器因此自有线程，完成结果通过 environment-scoped `napi_threadsafe_function` 送回 JavaScript 线程。

thread-safe function 只传递 native completion payload；它不是请求队列，也不改变 admission 上限。dispatcher 的 `max_queue_size` 固定为 64；只有 native worker 调用 `napi_call_threadsafe_function(..., napi_tsfn_blocking)`。因此 completion 满时 worker 会停在 transport backpressure 上，不能丢结果或留下永不 settled 的 Promise。JavaScript 线程绝不使用 blocking submit。environment cleanup 会先 abort dispatcher，从而唤醒被阻塞的 worker；调用返回 `napi_closing` 时，worker 释放 payload，不再访问 JavaScript。

### 7.3 Event-loop liveness

- idle、open 的 engine 不应阻止进程自然退出；environment dispatcher 默认 unref。
- `createEngine`、`recognize` 或显式 `close` 仍有未 settled Promise 时，在 JavaScript 主线程 ref dispatcher。
- 最后一个 Promise settled 后，在主线程 unref。
- ref/unref 不在 native worker 上调用。
- GC 触发的无 Promise close 不重新 ref event loop；若进程正在退出，由 environment cleanup 接管。

这使“未完成的显式异步调用会保持进程存活”和“仅仅忘记 close 一个 idle engine 不会永久挂住进程”同时成立。

## 8. 生命周期与关闭

### 8.1 显式 `close()`

`close()` 是幂等的：

1. 第一次调用在 JavaScript 线程把状态从 `open` 原子改为 `closing`，新 `recognize` 立即以 `invalid_engine` 拒绝。
2. 在当前 FIFO 尾部加入一个不受 queueCapacity 限制的 close control item。
3. worker 完成所有已接受 recognize，调用 Core `Engine::close()`，释放 bundle/session，然后退出。
4. JavaScript 线程收到 completion、join 已退出的 worker，并 resolve close Promise。
5. 并发或后续 `close()` 返回同一个 cached `Promise<void>`；它最终 resolve，不重复关闭。

显式 close 选择 drain，而不是隐式取消。只要 `recognize()` 已经返回一个已接受请求对应的 Promise，它就会在正常 environment 中得到 OCR 结果、错误，或由调用方 signal 触发的取消结果。

### 8.2 GC finalizer

JavaScript wrapper 用 `napi_wrap` 持有 `shared_ptr<EngineState>`，并用 `napi_type_tag_object`/`napi_check_object_type_tag` 校验 native receiver。finalizer 只做非阻塞操作：标记 closing、安排 drain/close、释放 wrapper 引用；不能在 GC 回调里等待推理或 join 线程。worker 退出前提交一个不关联 Promise 的 reap completion；若 event loop 仍运行，dispatcher callback 负责 join。该 completion 不 ref event loop；若来不及执行，environment cleanup 负责 abort transport 并 join。

每个 accepted work item 也持有 `EngineState`，因此即使 wrapper 被 GC，进行中的 Promise 和 native 状态也不会悬空。用户仍应显式 `await engine.close()`；GC 只是安全兜底，不是资源管理 API。

### 8.3 Environment teardown 与 `worker.terminate()`

每个 `napi_env` 有独立 `EnvContext`，不能使用 process-global `napi_env`、constructor reference 或 engine registry。模块初始化创建一个 environment-scoped dispatcher，并注册 `napi_add_env_cleanup_hook`。dispatcher 初始 thread count 为 1；每个 engine worker 在启动前 acquire、退出前 release。teardown 顺序是：

1. 标记 environment closing，拒绝新的 admission。
2. 在 Environment 主线程用 `napi_tsfn_abort` release dispatcher 的初始引用；这会禁止新 completion，并唤醒可能因 completion queue 已满而阻塞的 worker。
3. 通知所有 engine 丢弃尚未开始的队列项；此时 JavaScript 可能已禁止执行，不能尝试 settle Promise。
4. cleanup hook 同步等待当前 Core 调用自然返回；每个 worker 随后 close engine、release 自己的 dispatcher 引用并退出。
5. cleanup hook join 所有 engine worker 后返回，但不释放 `EnvContext`。
6. Node.js 在手工 cleanup hooks 之后调用 dispatcher finalizer；此时没有 worker、队列 payload 或 wrapper 路径再使用 `EnvContext`，finalizer 才释放它。

thread-safe function 的 JS callback 必须处理 `env == nullptr` 或 callback 不可用的 teardown 路径：只释放 native completion，不创建对象、不 settle Promise。

这里有意使用同步 environment cleanup，而不是让 async cleanup hook 依赖 dispatcher finalizer：Node.js 把 native finalizer 安排在手工 cleanup hooks 之后，后者会产生循环等待。Core/ONNX Runtime 当前没有安全的执行中断接口，所以 `worker.terminate()` 或进程 teardown 可能需要等待一个正在运行的 inference 返回；这段时间对应 Environment 已经不能执行 JavaScript。v1 不承诺硬超时；需要可强杀隔离时，应把 OCR 放在独立进程，而不是伪造线程取消。

### 8.4 状态行为表

| 操作 | loading | open | closing | closed/failed | environment closing |
| --- | --- | --- | --- | --- | --- |
| `createEngine` Promise | pending | resolve engine | — | reject | 不再执行 JS |
| `recognize` | engine 尚不可见 | accept 或 `queue_full` | `invalid_engine` | `invalid_engine` | `environment_closing`（若仍可返回） |
| `close` | engine 尚不可见 | 开始 drain | 返回既有 close 完成 | resolve | 由 cleanup 接管 |
| wrapper finalizer | — | 异步 drain/close | 释放 wrapper 引用 | no-op | no-op |

## 9. AbortSignal 与 cooperative cancellation

`RecognizeOptions.signal` 从 v1 起支持，但它取消的是请求交付，不虚构 Core/ONNX Runtime 硬中断。public API 没有独立 `cancel()`；private native cancel 只由 JS facade 的 signal listener 调用。

### 9.1 状态语义

| Abort 发生时点 | Public Promise | Native 行为 | 资源释放 |
| --- | --- | --- | --- |
| 调用前已 aborted | 立即按 `signal.reason` reject | 不 admission | 无 snapshot/slot |
| snapshot 后、仍 queued | 立即按 reason reject | 从 FIFO 移除，永不进入 Core | 立即释放 snapshot、request count、pending bytes |
| 已 running | 立即按 reason reject | 设置 `discard_result`；Core 继续到返回，不转换 JS result | Core 返回后释放 snapshot/count |
| completion 已进入 JS dispatcher、public Promise 尚未 settled | abort 与 completion 在 JS 线程线性竞争，先设置 public settled flag 者获胜 | 败方只清理，不重复 settle | 按原 completion 路径释放 |
| public Promise 已 settled | 无影响 | listener 已移除，native cancel 不调用 | 无变化 |

`signal.reason` 原样成为 rejection reason；不包装成 `OcrError`，也不新增伪 Core error code。默认 `AbortController.abort()` 的 reason 是标准 `AbortError`。如果调用方传入自定义 reason，就按原对象拒绝。

### 9.2 Race 与计数规则

- Request ID 在一个 `napi_env` 内单调且不复用；JS 用 `bigint` 持有，避免长期运行后超过 safe integer。
- private cancel 在 Environment 主线程锁住 engine queue/state 后返回 `queued_cancelled`、`running_discarded` 或 `already_terminal`。
- queued cancel 从 deque 移除并立即归还 request/snapshot budget；worker 与 cancel 不能同时拥有同一 work item。
- running cancel 不能提前归还 Core 正在使用的 snapshot，也不能提前开放同 engine 的执行槽；只是不再构造/交付 OCR result。
- public Promise、native deferred、request count、external-memory delta 和 signal listener 分别有一次性 guard，任何 race 都不得 double-settle 或 double-free。
- public Promise 因 abort settled 后，不再仅为该请求 ref event loop。若它是唯一工作，进程可以开始退出；environment cleanup 仍会等待当前 inference 安全返回。

### 9.3 明确限制

Abort 不保证节省已经开始的 CPU 时间，也不是安全隔离边界。需要硬 deadline 或立即回收计算资源时，调用方必须使用独立进程并终止进程。未来只有在 Core/ONNX Runtime 提供经过验证的安全 interruption 后，才可把 running cancel 升级为计算取消；该升级不能改变现有 Promise 语义。

## 10. 错误契约

Core 返回的 13 个 `ErrorCode` 逐字映射到 `OcrError.code`，message/detail 不改变含义。适配器新增五个稳定 code：

| Code | 条件 |
| --- | --- |
| `bundle_io_failed` | bundle 路径、权限、安全遍历或完整读取失败；尚未进入 Core bundle validation |
| `queue_full` | 当前 engine 的请求数或 pending-input byte budget 已被占满 |
| `environment_closing` | Node.js Environment 已开始 teardown，但当前调用仍能安全返回 rejection |
| `unsupported_platform` | 当前 OS、architecture 或 libc 不在发布支持矩阵 |
| `package_load_failed` | 支持的平台 package 或必需 model package 缺失、损坏、版本/identity 不匹配 |

结构性 API 错误使用 `invalid_argument`；image metadata/view 不合法使用 `invalid_image`；单请求超过 adapter/Core 限制使用 `resource_limit_exceeded`；close 后调用使用 `invalid_engine`。不把 queue backpressure 伪装成 inference failure。

Abort rejection 不是 `OcrError`，不进入上表。适配器必须原样保留 `signal.reason`，方便调用方使用标准 `AbortError` 或自定义取消原因。

异常实现规则：

- 每个 Node-API status 都检查；不能忽略 pending exception。
- native 入口捕获所有 C++ 异常，映射为 `OcrError`，不让异常穿过 C ABI。
- JavaScript 对象只能在 `napi_env` 所属线程构造。
- 错误和默认日志不得包含 raw pixels、tensor 或识别文本。
- 默认不写 stdout/stderr；诊断由返回值或测试 hook 提供。

## 11. Node-API 版本与依赖

实现直接包含 `node_api.h`，使用内部小型 C++ RAII helper，但不依赖 `node-addon-api`。原因是减少 wrapper ABI/异常配置差异，并让每个 Node-API status、线程边界和 teardown 分支可审计。

编译固定 `NAPI_VERSION=8`，因为 v8 已提供本设计需要的 object type tag，并覆盖 Promise、thread-safe function、detached ArrayBuffer 和 environment cleanup API，同时保留比 v9/v10 更宽的 Node-API ABI 兼容范围。较新的 Node.js 版本继续支持较低 Node-API version，所以不需要按 Node major 重编同一平台产物。

官方依据：

- [Node-API version matrix](https://nodejs.org/api/n-api.html#node-api-version-matrix)
- [`napi_add_env_cleanup_hook`](https://nodejs.org/api/n-api.html#napi_add_env_cleanup_hook)
- [Thread-safe functions](https://nodejs.org/api/n-api.html#asynchronous-thread-safe-function-calls)
- [Environment teardown and finalization](https://nodejs.org/api/n-api.html#finalization-on-the-exit-of-the-nodejs-environment)
- [Node.js release schedule](https://github.com/nodejs/release#release-schedule)

JavaScript package 的正式支持矩阵是：

| Runtime | v1 policy |
| --- | --- |
| Node.js 22.x | Tier 1，完整测试 |
| Node.js 24.x | Tier 1，完整测试 |
| Node.js 26.x | Current smoke；进入 LTS 后再提升为 Tier 1 |
| Node.js 20 及更早 | 不支持 |
| Electron | 未声明；需按 Electron major 做完整 lifecycle/prebuild 验证 |
| Bun | 未声明；需按其 Node-API 实现做兼容验证 |

`package.json.engines.node` 在 v1 发布时写为 `^22.0.0 || ^24.0.0`；Node 26 提升为 Tier 1 后再加入，不能把 smoke test 写成正式承诺。

## 12. npm 与二进制布局

当前源码布局：

```text
bindings/node/
  CMakeLists.txt
  exports-macos.txt
  exports-linux.map
  README.md
  src/
    addon.cpp
    bundle_loader.hpp
    bundle_loader.cpp
  js/
    index.cjs
    index.mjs
    index.d.ts
    load-native.cjs
  test/
    adapter.test.cjs
  package.json
```

发布包统一位于 `@arcships` scope。完整 layout、manifest、版本、发布顺序和 release gates 以 [npm package 设计](npm-packaging.md) 为准；本节只记录 N-API 相关边界。

```text
@arcships/light-ocr                              public JS/TS facade
@arcships/light-ocr-model-ppocrv6-small          required model bundle
@arcships/light-ocr-darwin-arm64                 addon + ONNX Runtime dylib + licenses
@arcships/light-ocr-darwin-x64                   addon + ONNX Runtime dylib + licenses
@arcships/light-ocr-win32-x64                    addon + onnxruntime.dll + licenses
@arcships/light-ocr-linux-x64-gnu                addon + ONNX Runtime so + licenses
```

Facade 同时提供 ESM 和 CommonJS exports，但两者加载同一个 environment-aware `.node` addon。native 子路径不作为 public export。model package 是 exact-version 普通 dependency；四个平台包是 exact-version optional dependencies，由 `os`、`cpu` 和 Linux `libc` metadata 筛选。

安装规则：

- 正常安装不运行 install/postinstall 脚本，不在用户机器隐式编译。模型只作为 npm package payload 由 npm 在安装阶段取得。
- model package 直接包含可读取的 bundle 目录；JS facade 解析绝对路径，native loader 不解压、不联网。
- 平台包包含 `.node`、锁定的 ONNX Runtime 动态库、third-party licenses、SBOM 和 artifact hash。
- macOS 保持最低 13.3；Windows 使用 MSVC 2022 x64；Linux x64 GNU 基线在首次 N-API release 前用专用构建容器固定，不能把 Ubuntu 24.04 runner 偶然产生的 glibc 要求当作长期 SDK 承诺。
- macOS/Linux 使用相对 loader path，Windows DLL 与 `.node` 同目录。
- source build 是显式开发命令，不是 install fallback。
- 使用 `--omit=optional` 会缺少 native package；facade 必须返回可操作的 `package_load_failed`，不能尝试下载或编译。

Node-API 解决 Node/V8 ABI 兼容，不消除 OS、architecture、libc、C++ runtime 和 ONNX Runtime 的平台差异。因此仍需四个平台的原生构建和加载测试。

## 13. 构建边界

根 CMake 增加默认关闭的 `LIGHT_OCR_BUILD_NODE`。启用后：

- `light_ocr_node.node` 链接仓库内 `light_ocr_core`，不复制 OCR 源码。
- Node headers 是显式、锁定的构建输入；发布产物只调用 Node-API v8 symbols。
- C++17、浮点选项、ORT/OpenCV/Clipper locks 与 Core 完全一致。
- addon 不导出 Core 私有符号；只导出 Node-API module initializer。
- Debug 构建可直接运行 Node 测试。ASan/UBSan 和 TSan addon 必须配套兼容的 instrumented Node host；macOS 官方签名 Node 可能禁止后加载 sanitizer runtime，不能把这种 host 拒绝误报为 addon 测试结果。release prebuild 在目标 OS 原生生成。

开发期可以用本机 CMake/Node headers 构建，但发布矩阵不得依赖未锁定的全局 node-gyp、Python 或在线下载。

## 14. 测试与验收矩阵

### 14.1 API 与字段 parity

- TypeScript compile tests 覆盖所有 public types、ESM 和 CJS。
- 每个 `EngineInfo`、`OcrResult`、diagnostics、rejected line、warning 和 timing 字段与 C++ 对照。
- 四种 pixel format、padding stride、view byteOffset、1×1、边界尺寸、bad stride、truncated view 和 unsupported orientation。
- 每个 Core error code 的逐字映射；五个 adapter error code 的稳定行为。
- 真实 PP-OCRv6 bundle 的 14-fixture golden parity；同一 raw bytes 的文本、confidence 和 box 与 Core 一致。

### 14.2 所有权、队列与并发

- 调用 `recognize` 后立即覆写原 Buffer，结果必须仍对应调用瞬间快照。
- SharedArrayBuffer、detached ArrayBuffer、错误 TypedArray 被拒绝。
- queueCapacity 和 pending-input byte budget 的边界、恢复和 `queue_full`。
- pre-aborted 不复制；queued abort 释放 slot/bytes；running abort 立即拒绝但 engine 直到 Core 返回仍保持 busy；completion/abort race 只 settle 一次。
- 自定义 `signal.reason` 原样返回；listener、native deferred、request ID 和 external-memory accounting 无泄漏。
- 单 engine FIFO、绝不并发进入 Core；两个 engine 能并行执行。
- snapshot 分配失败、入队失败和 completion 失败不泄漏 slot/bytes。
- 大 stride 只复制 required range，不越过 typed-array view。

### 14.3 生命周期

- init success/failure、close before/after work、close while queued/running、重复 close、close 后 recognize。
- wrapper GC with no work、GC with pending work、忘记 close 后进程自然退出。
- `worker_threads` 中独立加载、并行使用、正常退出和 `worker.terminate()`。
- environment teardown 时 completion callback 的 `env == nullptr` 分支。
- 反复 create/recognize/close 的 RSS、external-memory accounting 和 handle/thread leak。
- TSAN 验证 state/queue/finalizer/cleanup；ASan/UBSan 验证转换和 teardown；Linux LSan 验证最终释放。

### 14.4 Bundle loader 安全

- symlink/reparse point、根目录替换、成员替换、短读、权限失败和目录循环。
- 文件数量、单文件大小、总大小、路径深度、路径 traversal、重复和 malformed Unicode/Windows path。
- hash mismatch、缺文件、多文件、损坏 ONNX 和 schema 不兼容继续由 Core contract tests 覆盖。

### 14.5 性能与 event loop

- 同机、同 bundle、同 raw fixture、queue depth 0 下，对比 Node-API end-to-end 与 native executable，并保存 warm median/p95 比率。独立进程、非交错采样时仅作 observation；相同 native runtime 且受控交错采样时 hard gate 为 median `<= 1.10x`、p95 `<= 1.15x`。
- 分开报告同步 snapshot、queue wait、Core `timingUs.total` 和 JavaScript result materialization，不能把 queue wait 算进 Core timing。
- 用高频 timer/heartbeat 证明 inference 期间 JavaScript event loop 可继续运行；同步 snapshot 停顿单独报告。
- 多 engine benchmark 必须记录 engine 数、ORT thread 数、CPU 和内存，防止以隐式 oversubscription 制造错误结论。

### 14.6 npm package contract

- 从六个本地 `.tgz` 在 sterile 临时目录安装；CJS、ESM 和 types 都只能使用 package 内容，不能回读仓库。
- `createEngine()`、`createEngine({})` 和 `model: "ppocrv6-small"` 都加载同一 bundle ID；显式 `bundlePath` 仍工作；`model` 与 `bundlePath` 同时提供时拒绝。
- model package 缺失、bundle ID 不匹配、支持平台 package 缺失、unsupported platform 和 `--omit=optional` 分别得到稳定、可操作的错误。
- `--ignore-scripts` 安装正常；已安装后禁网运行正常；没有 postinstall、下载、解压或源码编译副作用。
- `npm pack --dry-run` inventory、tarball hashes、model payload hashes、licenses、SBOM 和 package exact versions 全部核对。

完整 release gate 在 macOS arm64、macOS x64、Windows x64、Linux x64 GNU 上分别运行 Node 22 和 24；Node 26 运行加载、创建、golden、close 和 worker teardown smoke。

## 15. 实施顺序

1. **完成**：建立 `bindings/node` CMake target、原始 Node-API status/error helper 和 TS public types。
2. **完成**：实现安全 bundle directory loader 与 async `createEngine`。
3. **完成**：实现 `EngineState`、专用 worker、双重有界 admission、input snapshot 和 request ID/state machine。
4. **完成**：实现 AbortSignal facade、private cancel、结果/错误的完整字段转换与真实 PP-OCRv6 golden test。
5. **完成**：实现 explicit close、GC finalizer、dispatcher ref/unref 和 teardown-safe environment cleanup。
6. **部分完成**：Node.js 22/macOS arm64 已覆盖 event-loop heartbeat、正常退出和未 close 的 worker environment teardown；worker termination、compatible-host sanitizer、leak 和多平台性能矩阵仍待补。
7. 固定 Linux GNU baseline，构建四个平台 prebuild，生成 licenses/SBOM/hashes。
8. 生成 model、四个平台 native 和 facade 六个 package，执行 sterile tarball install test；先发布五个依赖，最后发布 `@arcships/light-ocr` facade。

前五步完成前不能把 addon 称为可用；四平台矩阵、发布元数据和 prebuild 完成前不能称为 npm release ready。

## 16. 明确延期项

| 项目 | v1 状态 | 重新开启条件 |
| --- | --- | --- |
| 独立模型镜像/公开下载页 | 延期 | npm model package 已满足默认安装；仅在需要非 npm 分发时重开 |
| 无模型或多模型 package | 延期 | 有真实体积/语言/服务端部署需求并定义兼容策略 |
| WebP/GIF/PDF 解码 | 延期 | 独立格式语义、安全限制和测试矩阵获批；当前仅支持 JPEG/PNG |
| zero-copy/transfer | 延期 | 能证明 mutation、detachment、Worker 和 teardown 安全 |
| running inference 硬中断 | 延期 | Core 或隔离层提供经过验证的安全 interruption |
| Electron/Bun | 延期 | 独立 runtime/version/prebuild/lifecycle matrix 全绿 |
| Linux musl/arm64、Windows arm64 | 延期 | Core 和 addon 原生 Tier 1 证据齐全 |
| GPU EP | 延期 | 独立 bundle、设备选择、并发、内存和发布策略 |
