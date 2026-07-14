#include <node_api.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "bundle_loader.hpp"
#include "encoded_image.hpp"
#include "light_ocr/core.hpp"

namespace light_ocr::node {
namespace {

constexpr std::size_t kDefaultQueueCapacity = 4;
constexpr std::size_t kMaximumQueueCapacity = 64;
constexpr std::uint64_t kDefaultPendingInputBytes = 256ull * 1024 * 1024;
constexpr std::uint64_t kMaximumPendingInputBytes = 1024ull * 1024 * 1024;
constexpr std::size_t kCompletionQueueCapacity = 64;
constexpr double kMaximumSafeInteger = 9007199254740991.0;
constexpr napi_type_tag kEngineTypeTag{0xaec3925fba3d4b41ULL, 0x9748d5c80121e692ULL};

class AddonFailure : public std::runtime_error {
 public:
  AddonFailure(std::string code, std::string message, std::string detail = {})
      : std::runtime_error(std::move(message)),
        code_(std::move(code)),
        detail_(std::move(detail)) {}

  const std::string& code() const noexcept { return code_; }
  const std::string& detail() const noexcept { return detail_; }

 private:
  std::string code_;
  std::string detail_;
};

class NapiFailure final : public AddonFailure {
 public:
  explicit NapiFailure(std::string message)
      : AddonFailure("internal_error", std::move(message)) {}
};

void check(napi_env env, napi_status status, const char* operation) {
  if (status == napi_ok) return;
  const napi_extended_error_info* info = nullptr;
  napi_get_last_error_info(env, &info);
  std::string message(operation);
  if (info != nullptr && info->error_message != nullptr) {
    message += ": ";
    message += info->error_message;
  }
  throw NapiFailure(std::move(message));
}

napi_value undefined(napi_env env) {
  napi_value value = nullptr;
  check(env, napi_get_undefined(env, &value), "get undefined");
  return value;
}

napi_value string_value(napi_env env, const std::string& value) {
  napi_value result = nullptr;
  check(env, napi_create_string_utf8(env, value.data(), value.size(), &result),
        "create UTF-8 string");
  return result;
}

napi_value double_value(napi_env env, double value) {
  napi_value result = nullptr;
  check(env, napi_create_double(env, value, &result), "create number");
  return result;
}

napi_value uint32_value(napi_env env, std::uint32_t value) {
  napi_value result = nullptr;
  check(env, napi_create_uint32(env, value, &result), "create uint32");
  return result;
}

napi_value boolean_value(napi_env env, bool value) {
  napi_value result = nullptr;
  check(env, napi_get_boolean(env, value, &result), "create boolean");
  return result;
}

void set_named(napi_env env, napi_value object, const char* name, napi_value value) {
  check(env, napi_set_named_property(env, object, name, value), "set object property");
}

napi_value create_error_value(napi_env env, const std::string& code, const std::string& message,
                              const std::string& detail = {}) {
  napi_value error = nullptr;
  const auto message_value = string_value(env, message);
  check(env, napi_create_error(env, nullptr, message_value, &error), "create error");
  set_named(env, error, "name", string_value(env, "OcrError"));
  set_named(env, error, "code", string_value(env, code));
  if (!detail.empty()) set_named(env, error, "detail", string_value(env, detail));
  return error;
}

napi_value create_abort_error(napi_env env) {
  napi_value error = nullptr;
  const auto message = string_value(env, "The operation was aborted");
  check(env, napi_create_error(env, nullptr, message, &error), "create abort error");
  set_named(env, error, "name", string_value(env, "AbortError"));
  set_named(env, error, "code", string_value(env, "ABORT_ERR"));
  return error;
}

void throw_failure(napi_env env, const AddonFailure& failure) noexcept {
  try {
    const auto error = create_error_value(env, failure.code(), failure.what(), failure.detail());
    napi_throw(env, error);
  } catch (...) {
    napi_throw_error(env, "internal_error", failure.what());
  }
}

void throw_unknown_failure(napi_env env, const char* message) noexcept {
  try {
    const auto error = create_error_value(env, "internal_error", message);
    napi_throw(env, error);
  } catch (...) {
    napi_throw_error(env, "internal_error", message);
  }
}

bool has_own(napi_env env, napi_value object, const char* name) {
  const auto key = string_value(env, name);
  bool result = false;
  check(env, napi_has_own_property(env, object, key, &result), "check own property");
  return result;
}

napi_value get_named(napi_env env, napi_value object, const char* name) {
  napi_value value = nullptr;
  check(env, napi_get_named_property(env, object, name, &value), "get object property");
  return value;
}

bool is_undefined(napi_env env, napi_value value) {
  napi_valuetype type = napi_undefined;
  check(env, napi_typeof(env, value, &type), "inspect value type");
  return type == napi_undefined;
}

void require_object(napi_env env, napi_value value, const char* context) {
  napi_valuetype type = napi_undefined;
  check(env, napi_typeof(env, value, &type), "inspect object type");
  if (type != napi_object || value == nullptr) {
    throw AddonFailure("invalid_argument", std::string(context) + " must be an object");
  }
  bool array = false;
  check(env, napi_is_array(env, value, &array), "check object array type");
  if (array) {
    throw AddonFailure("invalid_argument", std::string(context) + " must not be an array");
  }
}

std::string get_string(napi_env env, napi_value value, const char* context) {
  napi_valuetype type = napi_undefined;
  check(env, napi_typeof(env, value, &type), "inspect string type");
  if (type != napi_string) {
    throw AddonFailure("invalid_argument", std::string(context) + " must be a string");
  }
  std::size_t length = 0;
  check(env, napi_get_value_string_utf8(env, value, nullptr, 0, &length),
        "measure UTF-8 string");
  std::vector<char> buffer(length + 1, '\0');
  std::size_t written = 0;
  check(env, napi_get_value_string_utf8(env, value, buffer.data(), buffer.size(), &written),
        "read UTF-8 string");
  std::string result(buffer.data(), written);
  if (result.find('\0') != std::string::npos) {
    throw AddonFailure("invalid_argument", std::string(context) + " contains NUL");
  }
  return result;
}

double get_number(napi_env env, napi_value value, const char* context) {
  napi_valuetype type = napi_undefined;
  check(env, napi_typeof(env, value, &type), "inspect number type");
  if (type != napi_number) {
    throw AddonFailure("invalid_argument", std::string(context) + " must be a number");
  }
  double result = 0;
  check(env, napi_get_value_double(env, value, &result), "read number");
  if (!std::isfinite(result)) {
    throw AddonFailure("invalid_argument", std::string(context) + " must be finite");
  }
  return result;
}

std::uint32_t get_u32(napi_env env, napi_value value, const char* context,
                      std::uint32_t minimum = 0) {
  const double number = get_number(env, value, context);
  if (std::floor(number) != number || number < minimum ||
      number > std::numeric_limits<std::uint32_t>::max()) {
    throw AddonFailure("invalid_argument", std::string(context) + " is outside uint32 range");
  }
  return static_cast<std::uint32_t>(number);
}

std::uint64_t get_safe_u64(napi_env env, napi_value value, const char* context,
                           std::uint64_t minimum = 0) {
  const double number = get_number(env, value, context);
  if (std::floor(number) != number || number < static_cast<double>(minimum) ||
      number > kMaximumSafeInteger) {
    throw AddonFailure("invalid_argument", std::string(context) + " must be a safe integer");
  }
  return static_cast<std::uint64_t>(number);
}

bool get_boolean(napi_env env, napi_value value, const char* context) {
  napi_valuetype type = napi_undefined;
  check(env, napi_typeof(env, value, &type), "inspect boolean type");
  if (type != napi_boolean) {
    throw AddonFailure("invalid_argument", std::string(context) + " must be a boolean");
  }
  bool result = false;
  check(env, napi_get_value_bool(env, value, &result), "read boolean");
  return result;
}

std::optional<napi_value> optional_named(napi_env env, napi_value object, const char* name) {
  if (!has_own(env, object, name)) return std::nullopt;
  const auto value = get_named(env, object, name);
  if (is_undefined(env, value)) return std::nullopt;
  return value;
}

void reject_unknown_properties(napi_env env, napi_value object,
                               const std::unordered_set<std::string>& allowed,
                               const char* context) {
  napi_value names = nullptr;
  check(env,
        napi_get_all_property_names(env, object, napi_key_own_only, napi_key_all_properties,
                                    napi_key_numbers_to_strings, &names),
        "enumerate object properties");
  std::uint32_t length = 0;
  check(env, napi_get_array_length(env, names, &length), "get property count");
  for (std::uint32_t index = 0; index < length; ++index) {
    napi_value key = nullptr;
    check(env, napi_get_element(env, names, index, &key), "get property key");
    napi_valuetype type = napi_undefined;
    check(env, napi_typeof(env, key, &type), "inspect property key");
    if (type != napi_string) {
      throw AddonFailure("invalid_argument", std::string(context) + " has an unknown symbol key");
    }
    const auto name = get_string(env, key, "property name");
    if (allowed.find(name) == allowed.end()) {
      throw AddonFailure("invalid_argument",
                         std::string(context) + " has unknown property: " + name);
    }
  }
}

std::string error_code_string(ErrorCode code) { return to_string(code); }

struct ParsedCreateOptions {
  std::filesystem::path bundle_path;
  EngineOptions core;
  std::size_t queue_capacity = kDefaultQueueCapacity;
  std::uint64_t max_pending_input_bytes = kDefaultPendingInputBytes;
};

ResourceLimits parse_resource_limits(napi_env env, napi_value value) {
  require_object(env, value, "reducedLimits");
  const std::unordered_set<std::string> allowed{
      "maxWidth",          "maxHeight",          "maxPixels",
      "maxDetectionSide", "maxDetectionCandidates", "maxRecognitionBatchSize",
      "maxDetectionTiles", "maxRecognitionWidth", "maxTemporaryBytes"};
  reject_unknown_properties(env, value, allowed, "reducedLimits");
  const std::array<const char*, 8> required{
      "maxWidth",          "maxHeight",          "maxPixels",
      "maxDetectionSide", "maxDetectionCandidates", "maxRecognitionBatchSize",
      "maxRecognitionWidth", "maxTemporaryBytes"};
  for (const auto* name : required) {
    if (!has_own(env, value, name)) {
      throw AddonFailure("invalid_argument",
                         "reducedLimits must contain all legacy fields");
    }
  }
  ResourceLimits limits;
  limits.max_width = get_u32(env, get_named(env, value, "maxWidth"), "maxWidth", 1);
  limits.max_height = get_u32(env, get_named(env, value, "maxHeight"), "maxHeight", 1);
  limits.max_pixels = get_safe_u64(env, get_named(env, value, "maxPixels"), "maxPixels", 1);
  limits.max_detection_side =
      get_u32(env, get_named(env, value, "maxDetectionSide"), "maxDetectionSide", 1);
  limits.max_detection_candidates = get_u32(
      env, get_named(env, value, "maxDetectionCandidates"), "maxDetectionCandidates", 1);
  if (const auto option = optional_named(env, value, "maxDetectionTiles")) {
    limits.max_detection_tiles =
        get_u32(env, *option, "maxDetectionTiles", 1);
  }
  limits.max_recognition_batch_size = get_u32(
      env, get_named(env, value, "maxRecognitionBatchSize"), "maxRecognitionBatchSize", 1);
  limits.max_recognition_width = get_u32(
      env, get_named(env, value, "maxRecognitionWidth"), "maxRecognitionWidth", 1);
  limits.max_temporary_bytes = get_safe_u64(
      env, get_named(env, value, "maxTemporaryBytes"), "maxTemporaryBytes", 1);
  limits.max_concurrent_calls = 1;
  return limits;
}

DetectionStrategy parse_detection_strategy(napi_env env, napi_value value) {
  const auto strategy = get_string(env, value, "detection.strategy");
  if (strategy == "bounded") return DetectionStrategy::bounded;
  if (strategy == "tiled") return DetectionStrategy::tiled;
  if (strategy == "upstreamExact") return DetectionStrategy::upstream_exact;
  throw AddonFailure("invalid_argument",
                     "detection.strategy must be bounded, tiled, or upstreamExact");
}

DetectionOptions parse_detection_options(napi_env env, napi_value value) {
  require_object(env, value, "detection");
  const std::unordered_set<std::string> allowed{"strategy", "maxSide"};
  reject_unknown_properties(env, value, allowed, "detection");
  DetectionOptions parsed;
  if (const auto option = optional_named(env, value, "strategy")) {
    parsed.strategy = parse_detection_strategy(env, *option);
  }
  if (const auto option = optional_named(env, value, "maxSide")) {
    parsed.max_side = get_u32(env, *option, "detection.maxSide", 1);
  }
  return parsed;
}

ParsedCreateOptions parse_create_options(napi_env env, napi_value value) {
  require_object(env, value, "createEngine options");
  const std::unordered_set<std::string> allowed{
      "bundlePath",          "intraOpThreads",   "interOpThreads",
      "recognitionScoreThreshold", "recognitionBatchSize", "reducedLimits",
      "queueCapacity",       "maxPendingInputBytes", "detection"};
  reject_unknown_properties(env, value, allowed, "createEngine options");
  if (!has_own(env, value, "bundlePath")) {
    throw AddonFailure("invalid_argument", "bundlePath is required");
  }
  const auto bundle_path_string =
      get_string(env, get_named(env, value, "bundlePath"), "bundlePath");
  ParsedCreateOptions parsed;
  parsed.bundle_path = std::filesystem::u8path(bundle_path_string);
  if (!parsed.bundle_path.is_absolute()) {
    throw AddonFailure("invalid_argument", "bundlePath must be absolute");
  }
  if (const auto option = optional_named(env, value, "intraOpThreads")) {
    parsed.core.intra_op_threads = get_u32(env, *option, "intraOpThreads", 1);
  }
  if (const auto option = optional_named(env, value, "interOpThreads")) {
    parsed.core.inter_op_threads = get_u32(env, *option, "interOpThreads", 1);
  }
  if (const auto option = optional_named(env, value, "recognitionScoreThreshold")) {
    const double score = get_number(env, *option, "recognitionScoreThreshold");
    if (score < 0 || score > 1) {
      throw AddonFailure("invalid_argument", "recognitionScoreThreshold must be in [0, 1]");
    }
    parsed.core.recognition_score_threshold = static_cast<float>(score);
  }
  if (const auto option = optional_named(env, value, "recognitionBatchSize")) {
    parsed.core.recognition_batch_size = get_u32(env, *option, "recognitionBatchSize", 1);
  }
  if (const auto option = optional_named(env, value, "reducedLimits")) {
    parsed.core.reduced_limits = parse_resource_limits(env, *option);
  }
  if (const auto option = optional_named(env, value, "detection")) {
    parsed.core.detection = parse_detection_options(env, *option);
  }
  if (const auto option = optional_named(env, value, "queueCapacity")) {
    parsed.queue_capacity = get_u32(env, *option, "queueCapacity", 1);
    if (parsed.queue_capacity > kMaximumQueueCapacity) {
      throw AddonFailure("invalid_argument", "queueCapacity must be in [1, 64]");
    }
  }
  if (const auto option = optional_named(env, value, "maxPendingInputBytes")) {
    parsed.max_pending_input_bytes =
        get_safe_u64(env, *option, "maxPendingInputBytes", 1);
    if (parsed.max_pending_input_bytes > kMaximumPendingInputBytes) {
      throw AddonFailure("invalid_argument", "maxPendingInputBytes exceeds 1 GiB");
    }
  }
  return parsed;
}

RecognizeOptions parse_recognize_options(napi_env env, napi_value value,
                                         const EngineInfo& info) {
  RecognizeOptions parsed;
  if (value == nullptr || is_undefined(env, value)) return parsed;
  require_object(env, value, "recognize options");
  const std::unordered_set<std::string> allowed{
      "recognitionScoreThreshold", "recognitionBatchSize", "includeDiagnostics",
      "useTextlineOrientation", "detectionMaxSide"};
  reject_unknown_properties(env, value, allowed, "recognize options");
  if (const auto option = optional_named(env, value, "recognitionScoreThreshold")) {
    const double score = get_number(env, *option, "recognitionScoreThreshold");
    if (score < 0 || score > 1) {
      throw AddonFailure("invalid_argument", "recognitionScoreThreshold must be in [0, 1]");
    }
    parsed.recognition_score_threshold = static_cast<float>(score);
  }
  if (const auto option = optional_named(env, value, "recognitionBatchSize")) {
    const auto batch = get_u32(env, *option, "recognitionBatchSize", 1);
    if (batch > info.limits.max_recognition_batch_size) {
      throw AddonFailure("invalid_argument", "recognitionBatchSize exceeds engine limits");
    }
    parsed.recognition_batch_size = batch;
  }
  if (const auto option = optional_named(env, value, "includeDiagnostics")) {
    parsed.include_diagnostics = get_boolean(env, *option, "includeDiagnostics");
  }
  if (const auto option = optional_named(env, value, "detectionMaxSide")) {
    parsed.detection_max_side =
        get_u32(env, *option, "detectionMaxSide", 1);
    if (info.detection_strategy != DetectionStrategy::bounded ||
        *parsed.detection_max_side > info.detection_max_side) {
      throw AddonFailure(
          "invalid_argument",
          "detectionMaxSide requires bounded mode and cannot increase the engine default");
    }
  }
  if (const auto option = optional_named(env, value, "useTextlineOrientation")) {
    parsed.use_textline_orientation = get_boolean(env, *option, "useTextlineOrientation");
    if (parsed.use_textline_orientation && !info.capabilities.textline_orientation) {
      throw AddonFailure("unsupported_capability",
                         "Text-line orientation is not available in this bundle");
    }
  }
  return parsed;
}

struct ParsedImage {
  const std::uint8_t* data = nullptr;
  std::size_t required_bytes = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::size_t stride = 0;
  PixelFormat pixel_format = PixelFormat::bgr8;
};

ParsedImage parse_image(napi_env env, napi_value value, const EngineInfo& info) {
  require_object(env, value, "image");
  const std::unordered_set<std::string> allowed{"data", "width", "height", "stride",
                                                 "pixelFormat"};
  reject_unknown_properties(env, value, allowed, "image");
  for (const auto& name : allowed) {
    if (!has_own(env, value, name.c_str())) {
      throw AddonFailure("invalid_image", "image is missing required field: " + name);
    }
  }

  const auto width = get_u32(env, get_named(env, value, "width"), "image.width", 1);
  const auto height = get_u32(env, get_named(env, value, "height"), "image.height", 1);
  const auto stride64 = get_safe_u64(env, get_named(env, value, "stride"), "image.stride", 1);
  if (stride64 > std::numeric_limits<std::size_t>::max()) {
    throw AddonFailure("invalid_image", "image.stride is not representable");
  }
  if (width > info.limits.max_width || height > info.limits.max_height ||
      static_cast<std::uint64_t>(width) * height > info.limits.max_pixels) {
    throw AddonFailure("resource_limit_exceeded", "image dimensions exceed engine limits");
  }

  const auto format = get_string(env, get_named(env, value, "pixelFormat"), "pixelFormat");
  std::size_t channels = 0;
  PixelFormat pixel_format = PixelFormat::bgr8;
  if (format == "gray8") {
    channels = 1;
    pixel_format = PixelFormat::gray8;
  } else if (format == "rgb8") {
    channels = 3;
    pixel_format = PixelFormat::rgb8;
  } else if (format == "bgr8") {
    channels = 3;
    pixel_format = PixelFormat::bgr8;
  } else if (format == "rgba8") {
    channels = 4;
    pixel_format = PixelFormat::rgba8;
  } else {
    throw AddonFailure("unsupported_pixel_format", "pixelFormat is unsupported", format);
  }

  const std::size_t stride = static_cast<std::size_t>(stride64);
  const std::uint64_t row_bytes = static_cast<std::uint64_t>(width) * channels;
  if (stride < row_bytes) throw AddonFailure("invalid_image", "image.stride is too small");
  const auto preceding_rows = static_cast<std::uint64_t>(height - 1);
  if (preceding_rows != 0 &&
      stride64 > std::numeric_limits<std::uint64_t>::max() / preceding_rows) {
    throw AddonFailure("invalid_image", "image byte extent overflows");
  }
  const std::uint64_t rows_before_last = preceding_rows * stride64;
  if (rows_before_last > std::numeric_limits<std::uint64_t>::max() - row_bytes) {
    throw AddonFailure("invalid_image", "image byte extent overflows");
  }
  const std::uint64_t required64 = rows_before_last + row_bytes;
  if (required64 > std::numeric_limits<std::size_t>::max()) {
    throw AddonFailure("invalid_image", "image byte extent is not representable");
  }

  const auto data_value = get_named(env, value, "data");
  bool typed = false;
  check(env, napi_is_typedarray(env, data_value, &typed), "check image typed array");
  if (!typed) throw AddonFailure("invalid_image", "image.data must be a Uint8Array");
  napi_typedarray_type type = napi_int8_array;
  std::size_t length = 0;
  void* data = nullptr;
  napi_value backing = nullptr;
  std::size_t byte_offset = 0;
  check(env,
        napi_get_typedarray_info(env, data_value, &type, &length, &data, &backing, &byte_offset),
        "read image typed array");
  (void)byte_offset;
  if (type != napi_uint8_array) {
    throw AddonFailure("invalid_image", "image.data must be a Uint8Array");
  }
  bool array_buffer = false;
  check(env, napi_is_arraybuffer(env, backing, &array_buffer), "check image ArrayBuffer");
  if (!array_buffer) {
    throw AddonFailure("invalid_image", "SharedArrayBuffer-backed images are unsupported");
  }
  bool detached = false;
  check(env, napi_is_detached_arraybuffer(env, backing, &detached), "check detached ArrayBuffer");
  if (detached) throw AddonFailure("invalid_image", "image.data ArrayBuffer is detached");
  const auto required = static_cast<std::size_t>(required64);
  if (length < required || (required != 0 && data == nullptr)) {
    throw AddonFailure("invalid_image", "image.data is truncated");
  }

  ParsedImage parsed;
  parsed.data = static_cast<const std::uint8_t*>(data);
  parsed.required_bytes = required;
  parsed.width = width;
  parsed.height = height;
  parsed.stride = stride;
  parsed.pixel_format = pixel_format;
  return parsed;
}

struct ImageSnapshot {
  std::vector<std::uint8_t> bytes;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::size_t stride = 0;
  PixelFormat pixel_format = PixelFormat::bgr8;
  bool encoded = false;
};

ImageSnapshot copy_image(const ParsedImage& parsed) {
  ImageSnapshot snapshot;
  snapshot.width = parsed.width;
  snapshot.height = parsed.height;
  snapshot.stride = parsed.stride;
  snapshot.pixel_format = parsed.pixel_format;
  snapshot.bytes.resize(parsed.required_bytes);
  if (parsed.required_bytes != 0) {
    std::memcpy(snapshot.bytes.data(), parsed.data, parsed.required_bytes);
  }
  return snapshot;
}

struct ParsedEncodedImage {
  const std::uint8_t* data = nullptr;
  std::size_t size = 0;
};

ParsedEncodedImage parse_encoded_image(napi_env env, napi_value value) {
  bool typed = false;
  check(env, napi_is_typedarray(env, value, &typed), "check encoded image typed array");
  if (!typed) {
    throw AddonFailure("invalid_image", "encoded image must be a Uint8Array");
  }
  napi_typedarray_type type = napi_int8_array;
  std::size_t length = 0;
  void* data = nullptr;
  napi_value backing = nullptr;
  std::size_t byte_offset = 0;
  check(env,
        napi_get_typedarray_info(env, value, &type, &length, &data, &backing, &byte_offset),
        "read encoded image typed array");
  (void)byte_offset;
  if (type != napi_uint8_array) {
    throw AddonFailure("invalid_image", "encoded image must be a Uint8Array");
  }
  bool array_buffer = false;
  check(env, napi_is_arraybuffer(env, backing, &array_buffer),
        "check encoded image ArrayBuffer");
  if (!array_buffer) {
    throw AddonFailure("invalid_image",
                       "SharedArrayBuffer-backed encoded images are unsupported");
  }
  bool detached = false;
  check(env, napi_is_detached_arraybuffer(env, backing, &detached),
        "check detached encoded image ArrayBuffer");
  if (detached) {
    throw AddonFailure("invalid_image", "encoded image ArrayBuffer is detached");
  }
  if (length == 0 || data == nullptr) {
    throw AddonFailure("invalid_image", "encoded image is empty");
  }
  return ParsedEncodedImage{static_cast<const std::uint8_t*>(data), length};
}

ImageSnapshot copy_encoded_image(const ParsedEncodedImage& parsed) {
  ImageSnapshot snapshot;
  snapshot.encoded = true;
  snapshot.bytes.resize(parsed.size);
  std::memcpy(snapshot.bytes.data(), parsed.data, parsed.size);
  return snapshot;
}

struct EnvContext;
struct EngineState;

enum class RequestStatus { queued, running, completion_queued, cancelled, settled };

struct Request {
  std::uint64_t id = 0;
  ImageSnapshot image;
  RecognizeOptions options;
  napi_deferred deferred = nullptr;
  RequestStatus status = RequestStatus::queued;
  bool discard_result = false;
  bool operation_live = false;
  std::uint64_t decode_us = 0;
};

enum class CompletionKind { create, recognize, maintenance, close, reap };

struct Completion {
  CompletionKind kind = CompletionKind::maintenance;
  std::shared_ptr<EngineState> engine;
  std::shared_ptr<Request> request;
  std::optional<OcrResult> result;
  std::optional<Error> error;
  struct AdapterError {
    std::string code;
    std::string message;
    std::string detail;
  };
  std::optional<AdapterError> adapter_error;
  std::int64_t external_memory_delta = 0;
};

struct EnvContext {
  napi_env env = nullptr;
  napi_threadsafe_function dispatcher = nullptr;
  std::mutex engines_mutex;
  std::vector<std::weak_ptr<EngineState>> engines;
  std::atomic<bool> closing{false};
  std::atomic<std::uint64_t> next_request_id{1};
  std::size_t outstanding_operations = 0;
};

enum class EngineStateValue { loading, open, closing, closed, failed };

struct EngineState : public std::enable_shared_from_this<EngineState> {
  EngineState(EnvContext* context_value, ParsedCreateOptions options_value,
              napi_deferred create_deferred_value)
      : context(context_value),
        create_options(std::move(options_value)),
        create_deferred(create_deferred_value) {}

  ~EngineState() {
    if (worker.joinable()) {
      if (worker.get_id() == std::this_thread::get_id()) {
        worker.detach();
      } else {
        worker.join();
      }
    }
  }

  void start();
  void run();
  void request_gc_close();
  void request_environment_close();
  void join();

  EnvContext* context;
  ParsedCreateOptions create_options;
  std::mutex mutex;
  std::mutex join_mutex;
  std::condition_variable changed;
  EngineStateValue state = EngineStateValue::loading;
  bool close_requested = false;
  bool environment_closing = false;
  bool joined = false;
  std::deque<std::shared_ptr<Request>> queue;
  std::unordered_map<std::uint64_t, std::shared_ptr<Request>> active;
  std::shared_ptr<Request> running;
  std::size_t pending_count = 0;
  std::uint64_t pending_input_bytes = 0;
  std::unique_ptr<Engine> core;
  EngineInfo info;
  std::uint64_t bundle_bytes = 0;
  std::thread worker;
  napi_deferred create_deferred = nullptr;
  bool create_operation_live = true;
  napi_deferred close_deferred = nullptr;
  bool close_operation_live = false;
};

void begin_operation(EnvContext* context) {
  if (context->closing.load()) {
    throw AddonFailure("environment_closing", "Node.js environment is closing");
  }
  if (context->outstanding_operations == 0) {
    check(context->env, napi_ref_threadsafe_function(context->env, context->dispatcher),
          "reference completion dispatcher");
  }
  ++context->outstanding_operations;
}

void end_operation(EnvContext* context) {
  if (context->closing.load() || context->outstanding_operations == 0) return;
  --context->outstanding_operations;
  if (context->outstanding_operations == 0) {
    check(context->env, napi_unref_threadsafe_function(context->env, context->dispatcher),
          "unreference completion dispatcher");
  }
}

bool post_completion(EnvContext* context, std::unique_ptr<Completion> completion) {
  const napi_status status = napi_call_threadsafe_function(
      context->dispatcher, completion.get(), napi_tsfn_blocking);
  if (status != napi_ok) return false;
  completion.release();
  return true;
}

void EngineState::start() {
  check(context->env, napi_acquire_threadsafe_function(context->dispatcher),
        "acquire completion dispatcher for engine worker");
  try {
    auto self = shared_from_this();
    worker = std::thread([self = std::move(self)] { self->run(); });
  } catch (...) {
    napi_release_threadsafe_function(context->dispatcher, napi_tsfn_release);
    throw;
  }
}

void EngineState::run() {
  try {
    auto loaded = load_bundle_directory_secure(create_options.bundle_path);
    bundle_bytes = loaded.total_bytes;
    auto bundle_result = ModelBundle::create(std::move(loaded.files));
    if (!bundle_result) {
      throw AddonFailure(error_code_string(bundle_result.error().code),
                         bundle_result.error().message, bundle_result.error().detail);
    }
    auto engine_result =
        Engine::create(std::move(bundle_result).value(), create_options.core);
    if (!engine_result) {
      throw AddonFailure(error_code_string(engine_result.error().code),
                         engine_result.error().message, engine_result.error().detail);
    }
    core = std::move(engine_result).value();
    info = core->info();
    {
      std::lock_guard<std::mutex> lock(mutex);
      state = EngineStateValue::open;
    }
    auto completion = std::make_unique<Completion>();
    completion->kind = CompletionKind::create;
    completion->engine = shared_from_this();
    completion->external_memory_delta = static_cast<std::int64_t>(bundle_bytes);
    if (!post_completion(context, std::move(completion))) {
      request_environment_close();
    }
  } catch (const BundleIoError& failure) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      state = EngineStateValue::failed;
    }
    auto completion = std::make_unique<Completion>();
    completion->kind = CompletionKind::create;
    completion->engine = shared_from_this();
    completion->adapter_error =
        Completion::AdapterError{"bundle_io_failed", "Failed to read model bundle", failure.what()};
    post_completion(context, std::move(completion));
    napi_release_threadsafe_function(context->dispatcher, napi_tsfn_release);
    return;
  } catch (const AddonFailure& failure) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      state = EngineStateValue::failed;
    }
    auto completion = std::make_unique<Completion>();
    completion->kind = CompletionKind::create;
    completion->engine = shared_from_this();
    completion->adapter_error =
        Completion::AdapterError{failure.code(), failure.what(), failure.detail()};
    post_completion(context, std::move(completion));
    napi_release_threadsafe_function(context->dispatcher, napi_tsfn_release);
    return;
  } catch (const std::exception& failure) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      state = EngineStateValue::failed;
    }
    auto completion = std::make_unique<Completion>();
    completion->kind = CompletionKind::create;
    completion->engine = shared_from_this();
    completion->adapter_error = Completion::AdapterError{
        "runtime_initialization_failed", "Unexpected adapter initialization failure",
        failure.what()};
    post_completion(context, std::move(completion));
    napi_release_threadsafe_function(context->dispatcher, napi_tsfn_release);
    return;
  } catch (...) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      state = EngineStateValue::failed;
    }
    auto completion = std::make_unique<Completion>();
    completion->kind = CompletionKind::create;
    completion->engine = shared_from_this();
    completion->adapter_error = Completion::AdapterError{
        "internal_error", "Unknown adapter initialization failure", {}};
    post_completion(context, std::move(completion));
    napi_release_threadsafe_function(context->dispatcher, napi_tsfn_release);
    return;
  }

  while (true) {
    std::shared_ptr<Request> request;
    {
      std::unique_lock<std::mutex> lock(mutex);
      changed.wait(lock, [this] {
        return environment_closing || !queue.empty() || close_requested;
      });
      if (environment_closing) break;
      if (!queue.empty()) {
        request = queue.front();
        queue.pop_front();
        request->status = RequestStatus::running;
        running = request;
      } else if (close_requested) {
        break;
      }
    }

    const auto snapshot_size = static_cast<std::uint64_t>(request->image.bytes.size());
    auto result = [&]() -> Result<OcrResult> {
      if (request->image.encoded) {
        const auto decode_begin = std::chrono::steady_clock::now();
        auto decoded_result = decode_encoded_image(request->image.bytes, info.limits);
        request->decode_us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - decode_begin)
                .count());
        if (!decoded_result) {
          return Result<OcrResult>::failure(decoded_result.error());
        }
        auto decoded = std::move(decoded_result).value();
        const ImageView view{decoded.bytes.data(), decoded.bytes.size(), decoded.width,
                             decoded.height, decoded.stride, decoded.pixel_format};
        return core->recognize(view, request->options);
      }
      const ImageView view{request->image.bytes.data(), request->image.bytes.size(),
                           request->image.width, request->image.height,
                           request->image.stride, request->image.pixel_format};
      return core->recognize(view, request->options);
    }();

    bool discard = false;
    {
      std::lock_guard<std::mutex> lock(mutex);
      std::vector<std::uint8_t>().swap(request->image.bytes);
      pending_input_bytes -= snapshot_size;
      running.reset();
      discard = request->discard_result || environment_closing;
      request->status = RequestStatus::completion_queued;
    }

    auto completion = std::make_unique<Completion>();
    completion->kind = discard ? CompletionKind::maintenance : CompletionKind::recognize;
    completion->engine = shared_from_this();
    completion->request = request;
    completion->external_memory_delta = -static_cast<std::int64_t>(snapshot_size);
    if (!discard) {
      if (result) {
        completion->result = std::move(result).value();
      } else {
        completion->error = result.error();
      }
    }
    const bool posted = post_completion(context, std::move(completion));
    {
      std::lock_guard<std::mutex> lock(mutex);
      --pending_count;
      if (!posted || discard) {
        active.erase(request->id);
        request->status = RequestStatus::settled;
      }
    }
    if (!posted) break;
  }

  if (core) {
    core->close();
    core.reset();
  }
  bool should_post_close = false;
  bool should_post_reap = false;
  {
    std::lock_guard<std::mutex> lock(mutex);
    state = EngineStateValue::closed;
    should_post_close = !environment_closing && close_deferred != nullptr;
    should_post_reap = !environment_closing && !should_post_close;
  }
  if (should_post_close || should_post_reap) {
    auto completion = std::make_unique<Completion>();
    completion->kind = should_post_close ? CompletionKind::close : CompletionKind::reap;
    completion->engine = shared_from_this();
    completion->external_memory_delta = -static_cast<std::int64_t>(bundle_bytes);
    post_completion(context, std::move(completion));
  }
  napi_release_threadsafe_function(context->dispatcher, napi_tsfn_release);
}

void EngineState::request_gc_close() {
  std::lock_guard<std::mutex> lock(mutex);
  if (environment_closing || state == EngineStateValue::closed ||
      state == EngineStateValue::failed) {
    return;
  }
  state = EngineStateValue::closing;
  close_requested = true;
  changed.notify_all();
}

void EngineState::request_environment_close() {
  std::deque<std::shared_ptr<Request>> discarded;
  {
    std::lock_guard<std::mutex> lock(mutex);
    environment_closing = true;
    close_requested = true;
    if (state == EngineStateValue::open) state = EngineStateValue::closing;
    discarded.swap(queue);
    for (const auto& request : discarded) {
      pending_input_bytes -= request->image.bytes.size();
      --pending_count;
      active.erase(request->id);
      request->status = RequestStatus::cancelled;
      request->deferred = nullptr;
      request->operation_live = false;
    }
    if (running) {
      running->discard_result = true;
      running->deferred = nullptr;
      running->operation_live = false;
    }
    changed.notify_all();
  }
}

void EngineState::join() {
  std::lock_guard<std::mutex> lock(join_mutex);
  if (joined || !worker.joinable()) return;
  joined = true;
  worker.join();
}

napi_value create_point(napi_env env, const Point& point) {
  napi_value object = nullptr;
  check(env, napi_create_object(env, &object), "create point");
  set_named(env, object, "x", double_value(env, point.x));
  set_named(env, object, "y", double_value(env, point.y));
  return object;
}

napi_value create_line(napi_env env, const OcrLine& line) {
  napi_value object = nullptr;
  check(env, napi_create_object(env, &object), "create OCR line");
  set_named(env, object, "text", string_value(env, line.text));
  set_named(env, object, "confidence", double_value(env, line.confidence));
  napi_value box = nullptr;
  check(env, napi_create_array_with_length(env, line.box.points.size(), &box), "create quad");
  for (std::size_t index = 0; index < line.box.points.size(); ++index) {
    check(env, napi_set_element(env, box, static_cast<std::uint32_t>(index),
                                create_point(env, line.box.points[index])),
          "set quad point");
  }
  set_named(env, object, "box", box);
  return object;
}

napi_value create_timing(napi_env env, const Timing& timing,
                         std::uint64_t decode_us) {
  napi_value object = nullptr;
  check(env, napi_create_object(env, &object), "create timing");
  const auto set = [&](const char* name, std::uint64_t value) {
    if (value > static_cast<std::uint64_t>(kMaximumSafeInteger)) {
      throw NapiFailure("timing exceeds JavaScript safe integer range");
    }
    set_named(env, object, name, double_value(env, static_cast<double>(value)));
  };
  if (decode_us > std::numeric_limits<std::uint64_t>::max() - timing.total_us) {
    throw NapiFailure("total timing overflows");
  }
  set("total", timing.total_us + decode_us);
  set("decode", decode_us);
  set("inputValidation", timing.input_validation_us);
  set("detectionPreprocess", timing.detection_preprocess_us);
  set("detectionInference", timing.detection_inference_us);
  set("detectionPostprocess", timing.detection_postprocess_us);
  set("detectionMerge", timing.detection_merge_us);
  set("cropAndSort", timing.crop_and_sort_us);
  set("recognitionPreprocess", timing.recognition_preprocess_us);
  set("recognitionInference", timing.recognition_inference_us);
  set("recognitionPostprocess", timing.recognition_postprocess_us);
  return object;
}

napi_value create_diagnostics(napi_env env, const Diagnostics& diagnostics) {
  napi_value object = nullptr;
  check(env, napi_create_object(env, &object), "create diagnostics");
  napi_value rejected = nullptr;
  check(env, napi_create_array_with_length(env, diagnostics.rejected_lines.size(), &rejected),
        "create rejected lines");
  for (std::size_t index = 0; index < diagnostics.rejected_lines.size(); ++index) {
    const auto& value = diagnostics.rejected_lines[index];
    napi_value entry = nullptr;
    check(env, napi_create_object(env, &entry), "create rejected line");
    set_named(env, entry, "line", create_line(env, value.line));
    set_named(env, entry, "reason",
              string_value(env, value.reason == RejectionReason::below_score_threshold
                                    ? "below_score_threshold"
                                    : "empty_decode"));
    check(env, napi_set_element(env, rejected, static_cast<std::uint32_t>(index), entry),
          "set rejected line");
  }
  set_named(env, object, "rejectedLines", rejected);

  napi_value warnings = nullptr;
  check(env, napi_create_array_with_length(env, diagnostics.warnings.size(), &warnings),
        "create warnings");
  for (std::size_t index = 0; index < diagnostics.warnings.size(); ++index) {
    napi_value warning = nullptr;
    check(env, napi_create_object(env, &warning), "create warning");
    set_named(env, warning, "code", string_value(env, diagnostics.warnings[index].code));
    set_named(env, warning, "message", string_value(env, diagnostics.warnings[index].message));
    check(env, napi_set_element(env, warnings, static_cast<std::uint32_t>(index), warning),
          "set warning");
  }
  set_named(env, object, "warnings", warnings);
  set_named(env, object, "detectedCandidates", uint32_value(env, diagnostics.detected_candidates));
  set_named(env, object, "acceptedBoxes", uint32_value(env, diagnostics.accepted_boxes));
  set_named(env, object, "detectionInputWidth",
            uint32_value(env, diagnostics.detection_input_width));
  set_named(env, object, "detectionInputHeight",
            uint32_value(env, diagnostics.detection_input_height));
  set_named(env, object, "rawDetectionBoxes",
            uint32_value(env, diagnostics.raw_detection_boxes));
  set_named(env, object, "suppressedDuplicateBoxes",
            uint32_value(env, diagnostics.suppressed_duplicate_boxes));
  set_named(env, object, "maxLiveDetectionPassBuffers",
            uint32_value(env, diagnostics.max_live_detection_pass_buffers));
  napi_value detection_passes = nullptr;
  check(env,
        napi_create_array_with_length(env, diagnostics.detection_passes.size(),
                                      &detection_passes),
        "create detection pass shapes");
  for (std::size_t index = 0; index < diagnostics.detection_passes.size(); ++index) {
    const auto& shape = diagnostics.detection_passes[index];
    napi_value entry = nullptr;
    check(env, napi_create_object(env, &entry), "create detection pass shape");
    set_named(env, entry, "tileOrdinal", uint32_value(env, shape.tile_ordinal));
    set_named(env, entry, "x", uint32_value(env, shape.x));
    set_named(env, entry, "y", uint32_value(env, shape.y));
    set_named(env, entry, "width", uint32_value(env, shape.width));
    set_named(env, entry, "height", uint32_value(env, shape.height));
    set_named(env, entry, "tensorWidth", uint32_value(env, shape.tensor_width));
    set_named(env, entry, "tensorHeight", uint32_value(env, shape.tensor_height));
    set_named(env, entry, "contourCandidates",
              uint32_value(env, shape.contour_candidates));
    set_named(env, entry, "rawCandidates",
              uint32_value(env, shape.raw_candidates));
    check(env,
          napi_set_element(env, detection_passes,
                           static_cast<std::uint32_t>(index), entry),
          "set detection pass shape");
  }
  set_named(env, object, "detectionPasses", detection_passes);
  napi_value batch_shapes = nullptr;
  check(env,
        napi_create_array_with_length(env, diagnostics.recognition_batch_shapes.size(),
                                      &batch_shapes),
        "create recognition batch shapes");
  for (std::size_t index = 0;
       index < diagnostics.recognition_batch_shapes.size(); ++index) {
    const auto& shape = diagnostics.recognition_batch_shapes[index];
    napi_value entry = nullptr;
    check(env, napi_create_object(env, &entry), "create recognition batch shape");
    set_named(env, entry, "batchSize", uint32_value(env, shape.batch_size));
    set_named(env, entry, "height", uint32_value(env, shape.height));
    set_named(env, entry, "width", uint32_value(env, shape.width));
    check(env,
          napi_set_element(env, batch_shapes, static_cast<std::uint32_t>(index),
                           entry),
          "set recognition batch shape");
  }
  set_named(env, object, "recognitionBatchShapes", batch_shapes);
  return object;
}

napi_value create_result(napi_env env, const OcrResult& result,
                         std::uint64_t decode_us) {
  napi_value object = nullptr;
  check(env, napi_create_object(env, &object), "create OCR result");
  napi_value lines = nullptr;
  check(env, napi_create_array_with_length(env, result.lines.size(), &lines), "create OCR lines");
  for (std::size_t index = 0; index < result.lines.size(); ++index) {
    check(env, napi_set_element(env, lines, static_cast<std::uint32_t>(index),
                                create_line(env, result.lines[index])),
          "set OCR line");
  }
  set_named(env, object, "lines", lines);
  set_named(env, object, "imageWidth", uint32_value(env, result.image_width));
  set_named(env, object, "imageHeight", uint32_value(env, result.image_height));
  set_named(env, object, "modelBundleId", string_value(env, result.model_bundle_id));
  set_named(env, object, "timingUs", create_timing(env, result.timing, decode_us));
  if (result.diagnostics) {
    set_named(env, object, "diagnostics", create_diagnostics(env, *result.diagnostics));
  }
  return object;
}

napi_value create_resource_limits(napi_env env, const ResourceLimits& limits) {
  napi_value object = nullptr;
  check(env, napi_create_object(env, &object), "create resource limits");
  set_named(env, object, "maxWidth", uint32_value(env, limits.max_width));
  set_named(env, object, "maxHeight", uint32_value(env, limits.max_height));
  set_named(env, object, "maxPixels", double_value(env, static_cast<double>(limits.max_pixels)));
  set_named(env, object, "maxDetectionSide", uint32_value(env, limits.max_detection_side));
  set_named(env, object, "maxDetectionCandidates",
            uint32_value(env, limits.max_detection_candidates));
  set_named(env, object, "maxDetectionTiles",
            uint32_value(env, limits.max_detection_tiles));
  set_named(env, object, "maxRecognitionBatchSize",
            uint32_value(env, limits.max_recognition_batch_size));
  set_named(env, object, "maxRecognitionWidth",
            uint32_value(env, limits.max_recognition_width));
  set_named(env, object, "maxTemporaryBytes",
            double_value(env, static_cast<double>(limits.max_temporary_bytes)));
  set_named(env, object, "maxConcurrentCalls", uint32_value(env, 1));
  return object;
}

napi_value create_engine_info(napi_env env, const EngineState& engine) {
  const auto& info = engine.info;
  napi_value object = nullptr;
  check(env, napi_create_object(env, &object), "create engine info");
  set_named(env, object, "coreVersion", string_value(env, info.core_version));
  set_named(env, object, "modelBundleId", string_value(env, info.model_bundle_id));
  set_named(env, object, "modelBundleSchemaVersion",
            string_value(env, info.model_bundle_schema_version));
  set_named(env, object, "normalizedConfigSchemaVersion",
            string_value(env, info.normalized_config_schema_version));
  set_named(env, object, "backend", string_value(env, info.backend));
  set_named(env, object, "executionProvider", string_value(env, info.execution_provider));
  napi_value capabilities = nullptr;
  check(env, napi_create_object(env, &capabilities), "create capabilities");
  set_named(env, capabilities, "detection", boolean_value(env, info.capabilities.detection));
  set_named(env, capabilities, "recognition", boolean_value(env, info.capabilities.recognition));
  set_named(env, capabilities, "textlineOrientation",
            boolean_value(env, info.capabilities.textline_orientation));
  set_named(env, capabilities, "tiledDetection",
            boolean_value(env, info.capabilities.tiled_detection));
  set_named(env, object, "capabilities", capabilities);
  set_named(env, object, "concurrencyMode", string_value(env, "serialized_reject_when_busy"));
  set_named(env, object, "limits", create_resource_limits(env, info.limits));
  set_named(env, object, "intraOpThreads", uint32_value(env, info.intra_op_threads));
  set_named(env, object, "interOpThreads", uint32_value(env, info.inter_op_threads));
  set_named(env, object, "detectionStrategy",
            string_value(env, info.detection_strategy == DetectionStrategy::bounded
                                  ? "bounded"
                                  : info.detection_strategy == DetectionStrategy::tiled
                                        ? "tiled"
                                  : "upstreamExact"));
  set_named(env, object, "detectionMaxSide",
            uint32_value(env, info.detection_max_side));
  if (info.tiled_detection) {
    napi_value tiled = nullptr;
    check(env, napi_create_object(env, &tiled), "create tiled detection info");
    set_named(env, tiled, "contractVersion",
              string_value(env, info.tiled_detection->contract_version));
    set_named(env, tiled, "tileSide",
              uint32_value(env, info.tiled_detection->tile_side));
    set_named(env, tiled, "minimumOverlap",
              uint32_value(env, info.tiled_detection->minimum_overlap));
    set_named(env, tiled, "artificialBoundaryMargin",
              uint32_value(env,
                           info.tiled_detection->artificial_boundary_margin));
    set_named(env, tiled, "mergeIouThreshold",
              double_value(env, info.tiled_detection->merge_iou_threshold));
    set_named(env, tiled, "mergeIosThreshold",
              double_value(env, info.tiled_detection->merge_ios_threshold));
    set_named(env, object, "tiledDetection", tiled);
  }
  set_named(env, object, "defaultRecognitionScoreThreshold",
            double_value(env, info.default_recognition_score_threshold));
  set_named(env, object, "defaultRecognitionBatchSize",
            uint32_value(env, info.default_recognition_batch_size));
  napi_value adapter = nullptr;
  check(env, napi_create_object(env, &adapter), "create adapter info");
  set_named(env, adapter, "scheduler", string_value(env, "dedicated_fifo"));
  set_named(env, adapter, "queueCapacity",
            double_value(env, static_cast<double>(engine.create_options.queue_capacity)));
  set_named(env, adapter, "maxPendingInputBytes",
            double_value(env, static_cast<double>(engine.create_options.max_pending_input_bytes)));
  set_named(env, object, "adapter", adapter);
  return object;
}

std::shared_ptr<EngineState> unwrap_engine(napi_env env, napi_value value) {
  bool tagged = false;
  check(env, napi_check_object_type_tag(env, value, &kEngineTypeTag, &tagged),
        "check engine type tag");
  if (!tagged) throw AddonFailure("invalid_engine", "Invalid OcrEngine receiver");
  void* data = nullptr;
  check(env, napi_unwrap(env, value, &data), "unwrap engine");
  if (data == nullptr) throw AddonFailure("invalid_engine", "OcrEngine has no native state");
  return *static_cast<std::shared_ptr<EngineState>*>(data);
}

napi_value native_recognize_impl(napi_env env, napi_callback_info callback_info,
                                 bool encoded) {
  std::shared_ptr<EngineState> engine;
  std::uint64_t snapshot_size = 0;
  bool reservation_live = false;
  bool external_memory_accounted = false;
  bool operation_live = false;
  bool enqueued = false;
  const auto rollback = [&]() noexcept {
    if (engine && reservation_live && !enqueued) {
      std::lock_guard<std::mutex> lock(engine->mutex);
      if (engine->pending_count != 0) --engine->pending_count;
      if (engine->pending_input_bytes >= snapshot_size) {
        engine->pending_input_bytes -= snapshot_size;
      }
    }
    if (external_memory_accounted) {
      std::int64_t adjusted = 0;
      napi_adjust_external_memory(env, -static_cast<std::int64_t>(snapshot_size), &adjusted);
    }
    if (operation_live && engine) {
      try {
        end_operation(engine->context);
      } catch (...) {
      }
    }
  };
  try {
    std::array<napi_value, 2> arguments{};
    std::size_t argument_count = arguments.size();
    napi_value receiver = nullptr;
    check(env, napi_get_cb_info(env, callback_info, &argument_count, arguments.data(), &receiver,
                                nullptr),
          "read recognize arguments");
    if (argument_count < 1) throw AddonFailure("invalid_argument", "image is required");
    engine = unwrap_engine(env, receiver);
    EngineInfo info;
    {
      std::lock_guard<std::mutex> lock(engine->mutex);
      if (engine->state != EngineStateValue::open) {
        throw AddonFailure("invalid_engine", "Engine is closed");
      }
      info = engine->info;
    }
    auto options = parse_recognize_options(
        env, argument_count >= 2 ? arguments[1] : nullptr, info);
    std::optional<ParsedImage> parsed_image;
    std::optional<ParsedEncodedImage> parsed_encoded_image;
    if (encoded) {
      parsed_encoded_image = parse_encoded_image(env, arguments[0]);
      snapshot_size = static_cast<std::uint64_t>(parsed_encoded_image->size);
    } else {
      parsed_image = parse_image(env, arguments[0], info);
      snapshot_size = static_cast<std::uint64_t>(parsed_image->required_bytes);
    }
    if (snapshot_size > engine->create_options.max_pending_input_bytes) {
      throw AddonFailure("resource_limit_exceeded",
                         "image snapshot exceeds maxPendingInputBytes");
    }

    {
      std::lock_guard<std::mutex> lock(engine->mutex);
      if (engine->state != EngineStateValue::open) {
        throw AddonFailure("invalid_engine", "Engine is closed");
      }
      if (engine->pending_count >= engine->create_options.queue_capacity ||
          snapshot_size > engine->create_options.max_pending_input_bytes -
                              engine->pending_input_bytes) {
        throw AddonFailure("queue_full", "Engine recognition queue is full");
      }
      ++engine->pending_count;
      engine->pending_input_bytes += snapshot_size;
      reservation_live = true;
    }

    auto snapshot = encoded ? copy_encoded_image(*parsed_encoded_image)
                            : copy_image(*parsed_image);
    auto request = std::make_shared<Request>();
    request->id = engine->context->next_request_id.fetch_add(1);
    if (request->id == 0) {
      throw AddonFailure("internal_error", "request ID space is exhausted");
    }
    request->image = std::move(snapshot);
    request->options = options;

    std::int64_t adjusted = 0;
    check(env, napi_adjust_external_memory(env, static_cast<std::int64_t>(snapshot_size), &adjusted),
          "account image snapshot");
    external_memory_accounted = true;
    begin_operation(engine->context);
    operation_live = true;

    napi_value operation = nullptr;
    {
      std::unique_lock<std::mutex> lock(engine->mutex);
      if (engine->state != EngineStateValue::open) {
        throw AddonFailure("invalid_engine", "Engine is closed");
      }
      const auto inserted = engine->active.emplace(request->id, request);
      if (!inserted.second) {
        throw AddonFailure("internal_error", "Duplicate recognition request ID");
      }
      try {
        engine->queue.push_back(request);
        napi_deferred deferred = nullptr;
        napi_value promise = nullptr;
        check(env, napi_create_promise(env, &deferred, &promise), "create recognize promise");
        check(env, napi_create_object(env, &operation), "create native recognition operation");
        napi_value request_id = nullptr;
        check(env, napi_create_bigint_uint64(env, request->id, &request_id), "create request ID");
        set_named(env, operation, "requestId", request_id);
        set_named(env, operation, "promise", promise);
        request->deferred = deferred;
        request->operation_live = true;
      } catch (...) {
        const auto queued = std::find(engine->queue.begin(), engine->queue.end(), request);
        if (queued != engine->queue.end()) engine->queue.erase(queued);
        engine->active.erase(request->id);
        throw;
      }
      enqueued = true;
    }
    engine->changed.notify_all();
    return operation;
  } catch (const AddonFailure& failure) {
    rollback();
    throw_failure(env, failure);
  } catch (const std::exception& failure) {
    rollback();
    throw_unknown_failure(env, failure.what());
  } catch (...) {
    rollback();
    throw_unknown_failure(env, "Unknown recognize admission failure");
  }
  return nullptr;
}

napi_value native_recognize(napi_env env, napi_callback_info callback_info) {
  return native_recognize_impl(env, callback_info, false);
}

napi_value native_recognize_encoded(napi_env env,
                                    napi_callback_info callback_info) {
  return native_recognize_impl(env, callback_info, true);
}

napi_value native_cancel(napi_env env, napi_callback_info callback_info) {
  try {
    napi_value argument = nullptr;
    std::size_t argument_count = 1;
    napi_value receiver = nullptr;
    check(env, napi_get_cb_info(env, callback_info, &argument_count, &argument, &receiver, nullptr),
          "read cancel arguments");
    if (argument_count != 1) throw AddonFailure("invalid_argument", "requestId is required");
    std::uint64_t request_id = 0;
    bool lossless = false;
    check(env, napi_get_value_bigint_uint64(env, argument, &request_id, &lossless),
          "read request ID");
    if (!lossless || request_id == 0) {
      throw AddonFailure("invalid_argument", "requestId is invalid");
    }
    auto engine = unwrap_engine(env, receiver);
    std::shared_ptr<Request> request;
    std::uint64_t released_bytes = 0;
    std::string status = "already_terminal";
    napi_deferred deferred = nullptr;
    bool end_live_operation = false;
    {
      std::lock_guard<std::mutex> lock(engine->mutex);
      const auto active = engine->active.find(request_id);
      if (active != engine->active.end()) {
        request = active->second;
        if (request->status == RequestStatus::queued) {
          const auto queued = std::find(engine->queue.begin(), engine->queue.end(), request);
          if (queued != engine->queue.end()) engine->queue.erase(queued);
          released_bytes = request->image.bytes.size();
          engine->pending_input_bytes -= released_bytes;
          --engine->pending_count;
          engine->active.erase(active);
          request->status = RequestStatus::cancelled;
          status = "queued_cancelled";
        } else if (request->status == RequestStatus::running ||
                   request->status == RequestStatus::completion_queued) {
          request->discard_result = true;
          status = "running_discarded";
        }
        if (request->deferred != nullptr) {
          deferred = request->deferred;
          request->deferred = nullptr;
        }
        if (request->operation_live) {
          request->operation_live = false;
          end_live_operation = true;
        }
      }
    }
    if (released_bytes != 0) {
      std::int64_t adjusted = 0;
      check(env,
            napi_adjust_external_memory(env, -static_cast<std::int64_t>(released_bytes), &adjusted),
            "release cancelled image snapshot accounting");
    }
    if (deferred != nullptr) {
      check(env, napi_reject_deferred(env, deferred, create_abort_error(env)),
            "reject cancelled native promise");
    }
    if (end_live_operation) end_operation(engine->context);
    return string_value(env, status);
  } catch (const AddonFailure& failure) {
    throw_failure(env, failure);
  } catch (const std::exception& failure) {
    throw_unknown_failure(env, failure.what());
  } catch (...) {
    throw_unknown_failure(env, "Unknown cancellation failure");
  }
  return nullptr;
}

napi_value native_close(napi_env env, napi_callback_info callback_info) {
  try {
    std::size_t argument_count = 0;
    napi_value receiver = nullptr;
    check(env, napi_get_cb_info(env, callback_info, &argument_count, nullptr, &receiver, nullptr),
          "read close receiver");
    auto engine = unwrap_engine(env, receiver);
    napi_deferred deferred = nullptr;
    napi_value promise = nullptr;
    check(env, napi_create_promise(env, &deferred, &promise), "create close promise");
    bool resolve_now = false;
    {
      std::lock_guard<std::mutex> lock(engine->mutex);
      if (engine->state == EngineStateValue::closed || engine->state == EngineStateValue::failed) {
        resolve_now = true;
      } else if (engine->close_deferred != nullptr) {
        throw AddonFailure("internal_error", "Native close called more than once");
      } else {
        engine->state = EngineStateValue::closing;
        engine->close_requested = true;
        engine->close_deferred = deferred;
        engine->close_operation_live = true;
        engine->changed.notify_all();
      }
    }
    if (resolve_now) {
      check(env, napi_resolve_deferred(env, deferred, undefined(env)), "resolve closed engine");
    } else {
      begin_operation(engine->context);
    }
    return promise;
  } catch (const AddonFailure& failure) {
    throw_failure(env, failure);
  } catch (const std::exception& failure) {
    throw_unknown_failure(env, failure.what());
  } catch (...) {
    throw_unknown_failure(env, "Unknown close failure");
  }
  return nullptr;
}

void finalize_engine(napi_env, void* data, void*) {
  auto* holder = static_cast<std::shared_ptr<EngineState>*>(data);
  if (holder != nullptr) {
    (*holder)->request_gc_close();
    delete holder;
  }
}

napi_value create_native_engine(napi_env env, const std::shared_ptr<EngineState>& engine) {
  napi_value object = nullptr;
  check(env, napi_create_object(env, &object), "create native engine");
  const std::array<napi_property_descriptor, 4> properties{{
      {"recognize", nullptr, native_recognize, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"recognizeEncoded", nullptr, native_recognize_encoded, nullptr, nullptr, nullptr,
       napi_default, nullptr},
      {"cancel", nullptr, native_cancel, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"close", nullptr, native_close, nullptr, nullptr, nullptr, napi_default, nullptr},
  }};
  check(env, napi_define_properties(env, object, properties.size(), properties.data()),
        "define native engine methods");
  set_named(env, object, "info", create_engine_info(env, *engine));
  auto* holder = new std::shared_ptr<EngineState>(engine);
  try {
    check(env, napi_wrap(env, object, holder, finalize_engine, nullptr, nullptr),
          "wrap native engine");
    check(env, napi_type_tag_object(env, object, &kEngineTypeTag), "tag native engine");
  } catch (...) {
    delete holder;
    throw;
  }
  return object;
}

void call_js(napi_env env, napi_value, void*, void* data) {
  std::unique_ptr<Completion> completion(static_cast<Completion*>(data));
  if (!completion || env == nullptr) return;
  try {
    auto& engine = completion->engine;
    if (completion->external_memory_delta != 0) {
      std::int64_t adjusted = 0;
      check(env, napi_adjust_external_memory(env, completion->external_memory_delta, &adjusted),
            "adjust native external memory");
    }
    if (completion->kind == CompletionKind::create) {
      napi_deferred deferred = nullptr;
      bool operation_live = false;
      {
        std::lock_guard<std::mutex> lock(engine->mutex);
        deferred = engine->create_deferred;
        engine->create_deferred = nullptr;
        operation_live = engine->create_operation_live;
        engine->create_operation_live = false;
      }
      if (completion->adapter_error) {
        const auto& error = *completion->adapter_error;
        check(env, napi_reject_deferred(env, deferred,
                                        create_error_value(env, error.code, error.message,
                                                           error.detail)),
              "reject engine creation");
        engine->join();
      } else {
        check(env, napi_resolve_deferred(env, deferred, create_native_engine(env, engine)),
              "resolve engine creation");
      }
      if (operation_live) end_operation(engine->context);
      return;
    }
    if (completion->kind == CompletionKind::recognize) {
      auto request = completion->request;
      napi_deferred deferred = nullptr;
      bool operation_live = false;
      {
        std::lock_guard<std::mutex> lock(engine->mutex);
        if (!request->discard_result) {
          deferred = request->deferred;
          request->deferred = nullptr;
        }
        operation_live = request->operation_live;
        request->operation_live = false;
        request->status = RequestStatus::settled;
        engine->active.erase(request->id);
      }
      if (deferred != nullptr) {
        if (completion->error) {
          const auto& error = *completion->error;
          check(env,
                napi_reject_deferred(env, deferred,
                                     create_error_value(env, error_code_string(error.code),
                                                        error.message, error.detail)),
                "reject recognition");
        } else {
          check(env, napi_resolve_deferred(
                         env, deferred,
                         create_result(env, *completion->result, request->decode_us)),
                "resolve recognition");
        }
      }
      if (operation_live) end_operation(engine->context);
      return;
    }
    if (completion->kind == CompletionKind::maintenance) return;
    if (completion->kind == CompletionKind::close) {
      napi_deferred deferred = nullptr;
      bool operation_live = false;
      {
        std::lock_guard<std::mutex> lock(engine->mutex);
        deferred = engine->close_deferred;
        engine->close_deferred = nullptr;
        operation_live = engine->close_operation_live;
        engine->close_operation_live = false;
      }
      engine->join();
      if (deferred != nullptr) {
        check(env, napi_resolve_deferred(env, deferred, undefined(env)), "resolve close");
      }
      if (operation_live) end_operation(engine->context);
      return;
    }
    if (completion->kind == CompletionKind::reap) engine->join();
  } catch (const std::exception& failure) {
    napi_fatal_error("light-ocr", NAPI_AUTO_LENGTH, failure.what(), NAPI_AUTO_LENGTH);
  } catch (...) {
    napi_fatal_error("light-ocr", NAPI_AUTO_LENGTH, "Unknown completion failure",
                     NAPI_AUTO_LENGTH);
  }
}

void finalize_dispatcher(napi_env, void* finalize_data, void*) {
  delete static_cast<EnvContext*>(finalize_data);
}

void cleanup_environment(void* data) {
  auto* context = static_cast<EnvContext*>(data);
  context->closing.store(true);
  napi_release_threadsafe_function(context->dispatcher, napi_tsfn_abort);
  std::vector<std::shared_ptr<EngineState>> engines;
  {
    std::lock_guard<std::mutex> lock(context->engines_mutex);
    for (const auto& weak : context->engines) {
      if (auto engine = weak.lock()) engines.push_back(std::move(engine));
    }
  }
  for (const auto& engine : engines) engine->request_environment_close();
  for (const auto& engine : engines) engine->join();
}

napi_value native_create_engine(napi_env env, napi_callback_info callback_info) {
  try {
    napi_value argument = nullptr;
    std::size_t argument_count = 1;
    check(env, napi_get_cb_info(env, callback_info, &argument_count, &argument, nullptr, nullptr),
          "read createEngine arguments");
    if (argument_count != 1) {
      throw AddonFailure("invalid_argument", "createEngine requires one options object");
    }
    auto parsed = parse_create_options(env, argument);
    napi_deferred deferred = nullptr;
    napi_value promise = nullptr;
    check(env, napi_create_promise(env, &deferred, &promise), "create engine promise");

    EnvContext* context = nullptr;
    check(env, napi_get_instance_data(env, reinterpret_cast<void**>(&context)),
          "get adapter environment state");
    if (context == nullptr || context->closing.load()) {
      throw AddonFailure("environment_closing", "Node.js environment is closing");
    }
    auto engine = std::make_shared<EngineState>(context, std::move(parsed), deferred);
    {
      std::lock_guard<std::mutex> lock(context->engines_mutex);
      context->engines.erase(
          std::remove_if(context->engines.begin(), context->engines.end(),
                         [](const std::weak_ptr<EngineState>& candidate) {
                           return candidate.expired();
                         }),
          context->engines.end());
      context->engines.push_back(engine);
    }
    begin_operation(context);
    try {
      engine->start();
    } catch (...) {
      end_operation(context);
      throw;
    }
    return promise;
  } catch (const AddonFailure& failure) {
    throw_failure(env, failure);
  } catch (const std::exception& failure) {
    throw_unknown_failure(env, failure.what());
  } catch (...) {
    throw_unknown_failure(env, "Unknown engine creation failure");
  }
  return nullptr;
}

napi_value initialize(napi_env env, napi_value exports) {
  auto context = std::make_unique<EnvContext>();
  context->env = env;
  napi_value resource_name = string_value(env, "light-ocr completion dispatcher");
  check(env,
        napi_create_threadsafe_function(
            env, nullptr, nullptr, resource_name, kCompletionQueueCapacity, 1, context.get(),
            finalize_dispatcher, nullptr, call_js, &context->dispatcher),
        "create completion dispatcher");
  check(env, napi_unref_threadsafe_function(env, context->dispatcher),
        "unreference idle completion dispatcher");
  check(env, napi_set_instance_data(env, context.get(), nullptr, nullptr),
        "set adapter environment state");
  check(env, napi_add_env_cleanup_hook(env, cleanup_environment, context.get()),
        "register adapter cleanup hook");
  napi_property_descriptor create_property{
      "createEngine", nullptr, native_create_engine, nullptr, nullptr, nullptr, napi_default, nullptr};
  check(env, napi_define_properties(env, exports, 1, &create_property),
        "export createEngine");
  context.release();
  return exports;
}

}  // namespace
}  // namespace light_ocr::node

NAPI_MODULE(NODE_GYP_MODULE_NAME, light_ocr::node::initialize)
