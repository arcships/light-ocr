#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "light_ocr/error.hpp"

namespace light_ocr {

enum class PixelFormat { gray8, rgb8, bgr8, rgba8 };

enum class DetectionStrategy { bounded, tiled, upstream_exact };

enum class ExecutionProvider { automatic, cpu, apple, webgpu };

enum class SessionFallback { error, cpu };

enum class CpuPartition { allow, forbid };

enum class PerformanceHint { latency, throughput };

enum class Precision { automatic, fp32, fp16 };

struct ImageView {
  const std::uint8_t* data = nullptr;
  std::size_t size = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::size_t stride = 0;
  PixelFormat pixel_format = PixelFormat::bgr8;
};

struct Point {
  float x = 0;
  float y = 0;
};

struct Quad {
  std::array<Point, 4> points{};
};

struct OcrLine {
  std::string text;
  float confidence = 0;
  Quad box;
};

enum class RejectionReason { below_score_threshold, empty_decode };

struct RejectedLine {
  OcrLine line;
  RejectionReason reason = RejectionReason::empty_decode;
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

struct DetectionOptions {
  std::optional<DetectionStrategy> strategy;
  std::optional<std::uint32_t> max_side;
};

struct ExecutionOptions {
  ExecutionProvider provider = ExecutionProvider::automatic;
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

enum class ConcurrencyMode { serialized_reject_when_busy };

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
  ExecutionProvider requested_provider = ExecutionProvider::automatic;
  SessionFallback session_fallback = SessionFallback::error;
  CpuPartition cpu_partition = CpuPartition::allow;
  std::optional<std::uint32_t> device_id;
  PerformanceHint performance_hint = PerformanceHint::latency;
  Precision requested_precision = Precision::automatic;
  std::vector<ProviderCapabilityInfo> provider_capabilities;
  CreationTrace selection_trace;
  SessionExecutionInfo detection;
  SessionExecutionInfo recognition;
};

struct EngineInfo {
  std::string core_version;
  std::string model_bundle_id;
  std::string model_bundle_schema_version;
  std::string normalized_config_schema_version;
  std::string backend;
  std::string execution_provider;
  ExecutionInfo execution;
  Capabilities capabilities;
  ConcurrencyMode concurrency_mode = ConcurrencyMode::serialized_reject_when_busy;
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
