#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
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

struct TiledDetectionConfig {
  std::string contract_version;
  std::uint32_t tile_side = 0;
  std::uint32_t minimum_overlap = 0;
  std::uint32_t dimension_multiple = 0;
  std::uint32_t artificial_boundary_margin = 0;
  double merge_iou_threshold = 0;
  double merge_ios_threshold = 0;
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

struct AppleModelConfig {
  std::string model_id;
  std::string package_path;
  std::string package_sha256;
  std::string input_name;
  std::string output_name;
  std::string shape_policy;
};

struct AppleProviderConfig {
  std::string minimum_macos;
  std::string device_policy;
  std::vector<std::string> architectures;
  std::vector<std::string> validated_device_families;
  std::string qualification_id;
  AppleModelConfig detection;
  AppleModelConfig recognition;
  std::uint32_t recognition_width_multiple = 1;
  std::uint32_t recognition_ane_maximum_width = 0;
  std::vector<std::uint32_t> recognition_runtime_width_buckets;
  std::uint32_t maximum_cached_functions = 1;
};

struct WebGpuModelConfig {
  std::string model_id;
  std::string model_path;
  std::string model_sha256;
  std::string source_model_id;
  std::string source_model_sha256;
};

struct WebGpuProviderConfig {
  std::string conversion_id;
  std::string graph_optimization_level;
  std::string cpu_partition;
  std::vector<std::string> required_cpu_operators;
  WebGpuModelConfig detection;
  WebGpuModelConfig recognition;
};

struct BundleData {
  std::string id;
  std::string schema_version;
  std::string normalized_config_schema_version;
  std::string detection_model_path;
  std::string detection_model_id;
  std::string detection_model_sha256;
  std::string recognition_model_path;
  std::string recognition_model_id;
  std::string recognition_model_sha256;
  std::unordered_map<std::string, SharedBytes> files;
  DetectionConfig detection;
  std::optional<TiledDetectionConfig> tiled_detection;
  DetectionStrategy default_detection_strategy = DetectionStrategy::upstream_exact;
  std::uint32_t default_detection_max_side = 4'000;
  GeometryConfig geometry;
  RecognitionConfig recognition;
  std::optional<AppleProviderConfig> apple_provider;
  std::optional<WebGpuProviderConfig> webgpu_provider;
  ResourceLimits limits;
  Capabilities capabilities;
};

}  // namespace light_ocr::internal
