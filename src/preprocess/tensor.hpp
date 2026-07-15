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

struct RecognitionBatchPlan {
  std::vector<RecognitionSample> samples;
};

Result<DetectionInput> make_detection_input(const cv::Mat& bgr,
                                            const DetectionConfig& config,
                                            DetectionStrategy strategy,
                                            std::uint32_t max_side,
                                            const ResourceLimits& limits);

Result<std::vector<RecognitionBatchPlan>> plan_recognition_batches(
    const std::vector<Quad>& boxes, const GeometryConfig& geometry,
    const RecognitionConfig& config, std::uint32_t batch_size,
    const ResourceLimits& limits, std::uint32_t tensor_width_multiple = 1,
    const std::vector<std::uint32_t>& tensor_width_buckets = {});

Result<RecognitionBatch> make_recognition_batch(
    const std::vector<cv::Mat>& crops, const RecognitionBatchPlan& plan,
    const RecognitionConfig& config, const ResourceLimits& limits,
    std::uint32_t tensor_width_multiple = 1,
    const std::vector<std::uint32_t>& tensor_width_buckets = {});

}  // namespace light_ocr::internal
