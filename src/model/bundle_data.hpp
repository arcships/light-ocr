#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "light_ocr/core.hpp"

namespace light_ocr::internal {

struct DetectionConfig {
  std::uint32_t limit_side_len = 0;
  std::string limit_type;
  std::uint32_t max_side_limit = 0;
  std::uint32_t dimension_multiple = 0;
  std::uint32_t minimum_dimension = 0;
  std::array<float, 3> mean{};
  std::array<float, 3> std{};
  float scale = 0;
  float threshold = 0;
  float box_threshold = 0;
  float unclip_ratio = 0;
  std::uint32_t max_candidates = 0;
  bool use_dilation = false;
  std::string score_mode;
  std::uint32_t minimum_box_side = 0;
};

struct GeometryConfig {
  std::uint32_t row_band_pixels = 0;
  float tall_line_ratio = 0;
};

struct RecognitionConfig {
  std::uint32_t channels = 0;
  std::uint32_t height = 0;
  std::uint32_t base_width = 0;
  std::uint32_t minimum_tensor_width = 0;
  std::uint32_t maximum_tensor_width = 0;
  std::array<float, 3> mean{};
  std::array<float, 3> std{};
  float scale = 0;
  float padding_value = 0;
  std::uint32_t default_batch_size = 0;
  std::uint32_t maximum_batch_size = 0;
  std::uint32_t blank_index = 0;
  bool collapse_repeats = true;
  float default_score_threshold = 0;
  std::vector<std::string> characters;
};

struct BundleData {
  std::string id;
  std::string schema_version;
  std::string detection_model_path;
  std::string recognition_model_path;
  std::unordered_map<std::string, SharedBytes> files;
  DetectionConfig detection;
  GeometryConfig geometry;
  RecognitionConfig recognition;
  ResourceLimits limits;
  Capabilities capabilities;
};

}  // namespace light_ocr::internal
