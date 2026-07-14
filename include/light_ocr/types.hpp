#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace light_ocr {

enum class PixelFormat { gray8, rgb8, bgr8, rgba8 };

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

enum class ConcurrencyMode { serialized_reject_when_busy };

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
  ConcurrencyMode concurrency_mode = ConcurrencyMode::serialized_reject_when_busy;
  ResourceLimits limits;
  std::uint32_t intra_op_threads = 1;
  std::uint32_t inter_op_threads = 1;
  float default_recognition_score_threshold = 0;
  std::uint32_t default_recognition_batch_size = 8;
};

}  // namespace light_ocr
