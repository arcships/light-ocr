# light-ocr Native C++ API

Status: Implemented source contract for Core 0.1.0  
Authority: public C++ source contract, ownership, lifecycle, errors, and compatibility  
Requirements: [requirements.md](requirements.md)  
Architecture: [architecture.md](architecture.md)

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

struct Diagnostics {
  std::vector<RejectedLine> rejected_lines;
  std::vector<DiagnosticWarning> warnings;
  std::uint32_t detected_candidates = 0;
  std::uint32_t accepted_boxes = 0;
};

struct Timing {
  std::uint64_t total_us = 0;
  std::uint64_t input_validation_us = 0;
  std::uint64_t detection_preprocess_us = 0;
  std::uint64_t detection_inference_us = 0;
  std::uint64_t detection_postprocess_us = 0;
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

Diagnostics are absent unless requested. They contain no raw pixels or tensors.

## 7. Options and limits

```cpp
namespace light_ocr {

struct ResourceLimits {
  std::uint32_t max_width = 10'000;
  std::uint32_t max_height = 10'000;
  std::uint64_t max_pixels = 40'000'000;
  std::uint32_t max_detection_side = 4'000;
  std::uint32_t max_detection_candidates = 3'000;
  std::uint32_t max_recognition_batch_size = 8;
  std::uint32_t max_recognition_width = 3'200;
  std::uint64_t max_temporary_bytes = 512ull * 1024 * 1024;
  std::uint32_t max_concurrent_calls = 1;
};

struct EngineOptions {
  std::uint32_t intra_op_threads = 1;
  std::uint32_t inter_op_threads = 1;
  std::optional<float> recognition_score_threshold;
  std::optional<std::uint32_t> recognition_batch_size;
  std::optional<ResourceLimits> reduced_limits;
};

struct RecognizeOptions {
  std::optional<float> recognition_score_threshold;
  std::optional<std::uint32_t> recognition_batch_size;
  bool include_diagnostics = false;
  bool use_textline_orientation = false;
};

}  // namespace light_ocr
```

Rules:

- Thread counts are positive and fixed at creation.
- Score thresholds are finite and in `[0, 1]`.
- Batch sizes are positive and no larger than the effective limit.
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
};

struct EngineInfo {
  std::string core_version;
  std::string model_bundle_id;
  std::string model_bundle_schema_version;
  std::string backend;
  std::string execution_provider;
  Capabilities capabilities;
  ConcurrencyMode concurrency_mode;
  ResourceLimits limits;
  std::uint32_t intra_op_threads = 1;
  std::uint32_t inter_op_threads = 1;
  float default_recognition_score_threshold = 0;
  std::uint32_t default_recognition_batch_size = 8;
};

}  // namespace light_ocr
```

`info` is an immutable creation snapshot. The returned reference remains valid until the engine object is destroyed, including after `close`.

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
3. Creates the ORT environment relationship.
4. Creates detection and recognition sessions.
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
