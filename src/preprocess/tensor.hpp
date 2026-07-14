#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <opencv2/core.hpp>

#include "light_ocr/error.hpp"
#include "light_ocr/types.hpp"
#include "model/bundle_data.hpp"

namespace light_ocr::internal {

struct DetectionInput {
  std::vector<float> values;
  std::vector<std::int64_t> shape;
  std::uint32_t original_width = 0;
  std::uint32_t original_height = 0;
  std::uint32_t resized_width = 0;
  std::uint32_t resized_height = 0;
};

struct RecognitionSample {
  std::size_t input_index = 0;
  std::uint32_t tensor_width = 0;
  std::uint32_t content_width = 0;
};

struct RecognitionBatch {
  std::vector<std::size_t> input_indices;
  std::vector<float> values;
  std::vector<std::int64_t> shape;
};

Result<DetectionInput> make_detection_input(const cv::Mat& bgr,
                                            const DetectionConfig& config,
                                            const ResourceLimits& limits);

Result<std::vector<RecognitionBatch>> make_recognition_batches(
    const std::vector<cv::Mat>& crops, const RecognitionConfig& config,
    std::uint32_t batch_size, const ResourceLimits& limits);

}  // namespace light_ocr::internal
