# light-ocr Native C++ API

Status: Core 0.2.0 tiled contract published；0.3.0 Apple and Native WebGPU provider source implemented and qualified on the recorded devices<br>
Authority: public C++ source contract, ownership, lifecycle, errors, and compatibility  
Requirements: [requirements.md](requirements.md)  
Architecture: [architecture.md](architecture.md)

The declarations below track the current source tree. Version 0.2.0 publishes the additive `DetectionStrategy::tiled` contract; the 0.3.0 candidate adds Apple and Native WebGPU providers plus descriptor-driven Auto selection.

## 1. Scope

The Core milestone exposes a C++17 source API. It is intended for the future in-repository N-API adapter and for native tests and development tools.

The project does not guarantee:

- Cross-compiler or cross-standard-library ABI compatibility.
- Stable class layout or symbol names.
- A public C ABI.
- A separately supported binary SDK.

Semantic behavior and serialized result meaning are stable within a released major version.

## 2. Header surface

Public headers are:

```text
include/light_ocr/error.hpp
include/light_ocr/types.hpp
include/light_ocr/core.hpp
```

Public headers use only the C++ standard library.

## 3. Error and result types

The API returns explicit results. Exceptions do not cross the public boundary.

```cpp
namespace light_ocr {

enum class ErrorCode {
  invalid_argument,
  invalid_image,
  unsupported_pixel_format,
  unsupported_capability,
  invalid_model_bundle,
  unsupported_model,
  model_integrity_failed,
  runtime_initialization_failed,
  inference_failed,
  postprocess_failed,
  resource_limit_exceeded,
  invalid_engine,
  internal_error,
};

struct Error {
  ErrorCode code;
  std::string message;
  std::string detail;
};

template <class T>
class Result {
 public:
  static Result success(T value);
  static Result failure(Error error);
  bool ok() const noexcept;
  explicit operator bool() const noexcept;
  const T& value() const&;
  T&& value() &&;
  const Error& error() const&;
};

}  // namespace light_ocr
```

`value()` on an error result is a caller contract violation and MAY terminate or throw a documented local logic exception. Library operations themselves return errors rather than throwing.

`detail` is optional and safe for diagnostics. It never contains recognized text, input pixels, model data, stack traces, or addresses.

## 4. Model bundle input

The core accepts immutable in-memory files:

```cpp
namespace light_ocr {

using SharedBytes = std::shared_ptr<const std::vector<std::uint8_t>>;

struct BundleFile {
  std::string path;
  SharedBytes bytes;
};

class ModelBundle {
 public:
  static Result<ModelBundle> create(std::vector<BundleFile> files);

  ModelBundle(ModelBundle&&) noexcept;
  ModelBundle& operator=(ModelBundle&&) noexcept;
  ~ModelBundle();
  ModelBundle(const ModelBundle&) = delete;
  ModelBundle& operator=(const ModelBundle&) = delete;

  const std::string& id() const noexcept;
  const std::string& schema_version() const noexcept;
};

}  // namespace light_ocr
```

Rules:

- Paths use forward slashes and are relative to the bundle root.
- Duplicate, absolute, parent-traversal, empty, or non-normalized paths are rejected.
- `create` validates schema, hashes, required files, configuration, and model identities.
- The bundle retains shared ownership of required byte buffers.
- File loading is a development-tool adapter and is not part of the core contract.

## 5. Image contract

```cpp
namespace light_ocr {

enum class PixelFormat {
  gray8,
  rgb8,
  bgr8,
  rgba8,
};

struct ImageView {
  const std::uint8_t* data = nullptr;
  std::size_t size = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::size_t stride = 0;
  PixelFormat pixel_format = PixelFormat::bgr8;
};

}  // namespace light_ocr
```

The minimum accessible byte count is:

```text
(height - 1) * stride + width * bytes_per_pixel
```

All operations are checked for overflow.

The caller owns the image memory. It must remain readable and unchanged until `recognize` returns. The engine never writes to it and retains no pointer after return.

RGBA bytes are interpreted in R, G, B, A order. Alpha is validated as part of the buffer but ignored. The library performs no premultiplication correction, alpha compositing, ICC conversion, or gamma conversion.

## 6. Geometry and results

```cpp
namespace light_ocr {

struct Point {
  float x = 0;
  float y = 0;
};

struct Quad {
  std::array<Point, 4> points;
};

struct OcrLine {
  std::string text;
  float confidence = 0;
  Quad box;
};

enum class RejectionReason {
  below_score_threshold,
  empty_decode,
};

struct RejectedLine {
  OcrLine line;
  RejectionReason reason;
};

struct DiagnosticWarning {
  std::string code;
  std::string message;
};

struct RecognitionBatchShape {
  std::uint32_t batch_size = 0;
  std::uint32_t height = 0;
  std::uint32_t width = 0;
  std::string compute_unit;
  std::string model_id;
  std::string shape_bucket;
};

struct DetectionPassShape {
  std::uint32_t tile_ordinal = 0;
  std::uint32_t x = 0;
  std::uint32_t y = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t tensor_width = 0;
  std::uint32_t tensor_height = 0;
  std::uint32_t contour_candidates = 0;
  std::uint32_t raw_candidates = 0;
};

struct Diagnostics {
  std::vector<RejectedLine> rejected_lines;
  std::vector<DiagnosticWarning> warnings;
  std::uint32_t detected_candidates = 0;
  std::uint32_t accepted_boxes = 0;
  std::uint32_t detection_input_width = 0;
  std::uint32_t detection_input_height = 0;
  std::uint32_t raw_detection_boxes = 0;
  std::uint32_t suppressed_duplicate_boxes = 0;
  std::uint32_t max_live_detection_pass_buffers = 0;
  std::vector<DetectionPassShape> detection_passes;
  std::vector<RecognitionBatchShape> recognition_batch_shapes;
};

struct Timing {
  std::uint64_t total_us = 0;
  std::uint64_t input_validation_us = 0;
  std::uint64_t detection_preprocess_us = 0;
  std::uint64_t detection_inference_us = 0;
  std::uint64_t detection_postprocess_us = 0;
  std::uint64_t detection_merge_us = 0;
  std::uint64_t crop_and_sort_us = 0;
  std::uint64_t recognition_preprocess_us = 0;
  std::uint64_t recognition_inference_us = 0;
  std::uint64_t recognition_postprocess_us = 0;
};

struct OcrResult {
  std::vector<OcrLine> lines;
  std::uint32_t image_width = 0;
  std::uint32_t image_height = 0;
  std::string model_bundle_id;
  Timing timing;
  std::optional<Diagnostics> diagnostics;
};

}  // namespace light_ocr
```

Geometry rules:

- The coordinate origin is the top-left image pixel.
- Points are a finite, non-degenerate, strictly convex quadrilateral in clockwise order from top-left.
- Coordinates are finite; restored DB coordinates follow the pinned PaddleOCR contract and are clamped to the inclusive image-edge ranges `x ∈ [0, image_width]` and `y ∈ [0, image_height]`. These are geometric edge coordinates, not array indices.
- Confidence is finite and in `[0, 1]`.
- Text is valid UTF-8.

Diagnostics are absent unless requested. They contain tensor shapes for parity and memory attribution, but no raw pixels or tensor values. In tiled mode, `detection_passes` is the row-major plan actually executed, `max_live_detection_pass_buffers` must be `1`, and `raw_detection_boxes - suppressed_duplicate_boxes == accepted_boxes`. The legacy aggregate input width/height are the maximum tensor dimensions across passes.

## 7. Options and limits

```cpp
namespace light_ocr {

struct ResourceLimits {
  std::uint32_t max_width = 10'000;
  std::uint32_t max_height = 10'000;
  std::uint64_t max_pixels = 40'000'000;
  std::uint32_t max_detection_side = 4'000;
  std::uint32_t max_detection_candidates = 3'000;
  std::uint32_t max_detection_tiles = 100;
  std::uint32_t max_recognition_batch_size = 8;
  std::uint32_t max_recognition_width = 3'200;
  std::uint64_t max_temporary_bytes = 512ull * 1024 * 1024;
  std::uint32_t max_concurrent_calls = 1;
};

enum class DetectionStrategy {
  bounded,
  tiled,
  upstream_exact,
};

struct DetectionOptions {
  std::optional<DetectionStrategy> strategy;
  std::optional<std::uint32_t> max_side;
};

enum class ExecutionProvider { cpu, apple };
enum class SessionFallback { error, cpu };
enum class CpuPartition { allow, forbid };
enum class PerformanceHint { latency, throughput };
enum class Precision { automatic, fp32, fp16 };

struct ExecutionOptions {
  ExecutionProvider provider = ExecutionProvider::cpu;
  SessionFallback session_fallback = SessionFallback::error;
  CpuPartition cpu_partition = CpuPartition::allow;
  std::optional<std::uint32_t> device_id;
  PerformanceHint performance_hint = PerformanceHint::latency;
  Precision precision = Precision::automatic;
};

struct EngineOptions {
  std::uint32_t intra_op_threads = 1;
  std::uint32_t inter_op_threads = 1;
  std::optional<float> recognition_score_threshold;
  std::optional<std::uint32_t> recognition_batch_size;
  std::optional<ResourceLimits> reduced_limits;
  DetectionOptions detection;
  ExecutionOptions execution;
};

struct RecognizeOptions {
  std::optional<float> recognition_score_threshold;
  std::optional<std::uint32_t> recognition_batch_size;
  bool include_diagnostics = false;
  bool use_textline_orientation = false;
  std::optional<std::uint32_t> detection_max_side;
};

}  // namespace light_ocr
```

Rules:

- Thread counts are positive and fixed at creation.
- Auto is the source-candidate default and accepts only provider-neutral options. Its ordered candidates come from the immutable platform runtime descriptor and always end in CPU; a CPU-only package resolves directly to `[cpu]`.
- Explicit CPU accepts `auto`/`fp32`, requires `cpuPartition=allow`, `sessionFallback=error`, and uses the existing ONNX Runtime path. Explicit providers attempt only the requested backend.
- Explicit WebGPU accepts `auto`/`fp32`, requires the published Linux x64/Vulkan or Windows x64/D3D12 runtime, and uses the bounded `Concat`/`Gather`/`Slice` CPU partition when `cpuPartition=allow`. `cpuPartition=forbid` fails closed; WebGPU `fp16` is not a public `0.3.0` profile and returns `invalid_argument`.
- The Apple provider accepts `auto`/`fp16`, bounded detection no larger than 960, recognition batch 1, and latency mode. Production bundles use open macOS compatibility: Apple Silicon with `cpuPartition=allow` selects FP16 ANE plus the width-based FP16 GPU route, while Intel Mac selects Core ML CPU+GPU. `cpuPartition=forbid` selects the all-GPU strict path and is accepted only on Apple Silicon.
- `sessionFallback` is a migration field whose only valid value is `error`; `sessionFallback=cpu` returns `invalid_argument`. Only Auto may continue to another backend during creation, using D112 typed reasons and a structured selection trace. Inference-time failures never retry on CPU.
- Apple execution requires a schema 1.1 bundle containing the hash-locked provider payload. Unsupported device IDs, throughput mode, precision/provider combinations, detection strategies, and batch sizes fail instead of being ignored.
- Score thresholds are finite and in `[0, 1]`.
- Batch sizes are positive and no larger than the effective limit.
- `bounded` defaults to side 960; its side is a positive 32 multiple no larger than the effective detection ceiling.
- `upstream_exact` uses the source 4,000 ceiling and cannot carry a separate `max_side`.
- `tiled` is selected only at engine creation, requires a validated `tiled-v1` bundle profile, reports pass side 1280, and rejects both engine and request side overrides.
- `max_detection_candidates` is a whole-image ceiling for tiled passes; overflow fails the request instead of returning a truncated result. `max_detection_tiles` is planned and checked before the first inference.
- A request may lower a bounded engine's side; it cannot raise it or change strategy.
- Engine limits may only reduce bundle limits.
- `max_temporary_bytes` bounds Core-owned converted images, crops, and input tensor buffers with checked arithmetic. ONNX Runtime's internal allocator/workspace is controlled by the pinned backend but is not included in this preflight counter; repeated lifecycle RSS and platform memory reports cover that process-level behavior.
- Text-line orientation `true` returns `unsupported_capability` for the Core bundle.
- Unknown options are impossible at the typed C++ boundary; future serialized adapters reject unknown enum values.

## 8. Engine information

```cpp
namespace light_ocr {

enum class ConcurrencyMode {
  serialized_reject_when_busy,
};

struct Capabilities {
  bool detection = true;
  bool recognition = true;
  bool textline_orientation = false;
  bool tiled_detection = false;
};

struct TiledDetectionInfo {
  std::string contract_version;
  std::uint32_t tile_side = 0;
  std::uint32_t minimum_overlap = 0;
  std::uint32_t artificial_boundary_margin = 0;
  float merge_iou_threshold = 0;
  float merge_ios_threshold = 0;
};

struct ProviderCapabilityInfo {
  std::string provider;
  bool package_included = false;
  bool device_available = false;
  bool device_validated = false;
};

struct SessionExecutionInfo {
  std::string requested_provider;
  std::vector<std::string> actual_provider_chain;
  std::string device;
  std::string device_family;
  std::string operating_system;
  std::string precision;
  std::string shape_policy;
  std::string model_id;
  std::string model_sha256;
  std::string runtime;
  std::string runtime_version;
  std::string provider_version;
  std::string model_cache_status;
  std::string qualification_id;
  bool device_validated = false;
  bool session_fallback = false;
  std::optional<std::string> fallback_reason;
};

struct ExecutionInfo {
  ExecutionProvider requested_provider;
  SessionFallback session_fallback;
  CpuPartition cpu_partition;
  std::optional<std::uint32_t> device_id;
  PerformanceHint performance_hint;
  Precision requested_precision;
  std::vector<ProviderCapabilityInfo> provider_capabilities;
  SessionExecutionInfo detection;
  SessionExecutionInfo recognition;
};

struct EngineInfo {
  std::string core_version;
  std::string model_bundle_id;
  std::string model_bundle_schema_version;
  std::string normalized_config_schema_version;
  std::string backend;
  // Compatibility aggregate. Prefer execution.detection/recognition.
  std::string execution_provider;
  ExecutionInfo execution;
  Capabilities capabilities;
  ConcurrencyMode concurrency_mode;
  ResourceLimits limits;
  std::uint32_t intra_op_threads = 1;
  std::uint32_t inter_op_threads = 1;
  DetectionStrategy detection_strategy = DetectionStrategy::bounded;
  std::uint32_t detection_max_side = 960;
  std::optional<TiledDetectionInfo> tiled_detection;
  float default_recognition_score_threshold = 0;
  std::uint32_t default_recognition_batch_size = 1;
};

}  // namespace light_ocr
```

`info` is an immutable creation snapshot. The returned reference remains valid until the engine object is destroyed, including after `close`. `provider_capabilities` separately reports package inclusion, runtime device availability, and whether the current hardware family has reviewed evidence. Each session repeats `device_validated` beside what was actually configured; `false` means the open compatibility path is experimental, not that Core ML was skipped. `RecognitionBatchShape` adds the per-request model/function bucket and ANE/GPU/CPU route. A configured provider chain is not itself placement proof: the Apple release gate separately checks every Core ML function with Compute Plan evidence and binds the result through `qualification_id`.

## 9. Engine API

```cpp
namespace light_ocr {

class Engine {
 public:
  static Result<std::unique_ptr<Engine>> create(
      ModelBundle bundle,
      const EngineOptions& options = {});

  virtual ~Engine() noexcept;

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;
  Engine(Engine&&) = delete;
  Engine& operator=(Engine&&) = delete;

  virtual Result<OcrResult> recognize(
      const ImageView& image,
      const RecognizeOptions& options = {}) noexcept = 0;

  virtual const EngineInfo& info() const noexcept = 0;
  virtual void close() noexcept = 0;
};

}  // namespace light_ocr
```

The concrete implementation is hidden behind the factory.

## 10. Lifecycle semantics

### 10.1 Creation

`Engine::create`:

1. Validates engine options.
2. Revalidates the bundle compatibility contract.
3. Selects the bundled inference backend from the validated execution policy.
4. Creates detection and recognition sessions independently.
5. Validates session inputs and outputs.
6. Publishes a Ready engine.

If any step fails, all acquired resources are released and no engine is returned.

### 10.2 Recognition

`recognize`:

- Is synchronous.
- Attempts non-blocking admission.
- Returns `resource_limit_exceeded` when another call owns the engine.
- Returns `invalid_engine` after close begins.
- Returns no partial result on failure.

### 10.3 Closing

`close`:

- Is thread-safe and idempotent.
- Prevents new recognition admission.
- Waits for an already admitted call.
- Releases sessions, retained buffers, and model storage.
- Does not throw.

The destructor calls `close`.

The caller must not destroy the `Engine` object while another thread is entering a member function. Standard C++ object-lifetime rules still apply.

## 11. Error mapping

| Condition | Code |
| --- | --- |
| Null data, zero dimensions, invalid stride, truncated buffer | `invalid_image` |
| Valid image exceeds declared limits | `resource_limit_exceeded` |
| Unsupported future pixel enum | `unsupported_pixel_format` |
| Orientation requested without capability | `unsupported_capability` |
| Missing or malformed manifest/config | `invalid_model_bundle` |
| Payload hash mismatch | `model_integrity_failed` |
| Model input/output contract mismatch | `unsupported_model` |
| ORT session creation | `runtime_initialization_failed` |
| ORT Run failure | `inference_failed` |
| Unsafe polygon or decode contract failure | `postprocess_failed` |
| Concurrent call on one engine | `resource_limit_exceeded` |
| Recognition after close | `invalid_engine` |
| Unexpected exception | `internal_error` |

## 12. Compatibility policy

- Major versions may change public source contracts and result semantics.
- Minor versions may add fields, enum values, capabilities, and overloads without changing existing behavior.
- Patch versions fix defects without intentionally changing golden results.
- A behavior change caused by a model update requires a new bundle ID even when the core version is unchanged.
- No release claims ABI compatibility across compilers, standard libraries, build types, or dependency versions.

## 13. Future adapter contract

A future asynchronous adapter must:

- Copy or otherwise retain input bytes before returning control to its caller.
- Own its worker pool and bounded queue.
- Keep an engine alive until accepted work completes.
- Map every `ErrorCode` without changing its meaning.
- Preserve all geometry, confidence, timing, and diagnostic semantics.

The accepted Node.js mapping is specified in [napi-design.md](napi-design.md). These requirements do not add asynchronous behavior to the Core API.
