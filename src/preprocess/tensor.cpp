#include "preprocess/tensor.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <numeric>
#include <utility>

#include <opencv2/imgproc.hpp>

#include "geometry/geometry.hpp"
#include "util/checked_math.hpp"

namespace light_ocr::internal {
namespace {

template <class T>
Result<T> failure(ErrorCode code, const char* message, std::string detail = {}) {
  return Result<T>::failure(Error{code, message, std::move(detail)});
}

std::uint32_t round_multiple_half_even(std::uint32_t value, std::uint32_t multiple) {
  const auto quotient = value / multiple;
  const auto remainder = value % multiple;
  const auto half = multiple / 2;
  if (remainder < half) return quotient * multiple;
  if (remainder > half) return (quotient + 1) * multiple;
  return ((quotient & 1u) == 0u ? quotient : quotient + 1u) * multiple;
}

std::uint32_t round_multiple_up(std::uint32_t value, std::uint32_t multiple) {
  return ((value + multiple - 1) / multiple) * multiple;
}

bool tensor_bytes(std::uint64_t elements, std::uint64_t* bytes) {
  return checked_mul<std::uint64_t>(elements, sizeof(float), bytes);
}

Result<RecognitionSample> make_recognition_sample(
    std::size_t input_index, std::uint32_t crop_width,
    std::uint32_t crop_height, const RecognitionConfig& config,
    const ResourceLimits& limits, std::uint32_t tensor_width_multiple,
    const std::vector<std::uint32_t>& tensor_width_buckets) {
  if (crop_width == 0 || crop_height == 0) {
    return failure<RecognitionSample>(ErrorCode::postprocess_failed,
                                      "Recognition crop is empty");
  }
  const double ratio = static_cast<double>(crop_width) / crop_height;
  const double base_ratio = static_cast<double>(config.base_width) / config.height;
  auto tensor_width = static_cast<std::uint32_t>(
      static_cast<double>(config.height) * std::max(base_ratio, ratio));
  tensor_width = std::max(config.minimum_tensor_width,
                          std::min(config.maximum_tensor_width, tensor_width));
  if (tensor_width_multiple == 0 ||
      tensor_width_multiple > config.maximum_tensor_width ||
      config.maximum_tensor_width % tensor_width_multiple != 0) {
    return failure<RecognitionSample>(ErrorCode::invalid_argument,
                                      "Recognition width multiple is invalid");
  }
  tensor_width = std::min(
      config.maximum_tensor_width,
      round_multiple_up(tensor_width, tensor_width_multiple));
  if (!tensor_width_buckets.empty()) {
    const bool valid_buckets =
        std::is_sorted(tensor_width_buckets.begin(), tensor_width_buckets.end()) &&
        std::adjacent_find(tensor_width_buckets.begin(),
                           tensor_width_buckets.end()) ==
            tensor_width_buckets.end() &&
        tensor_width_buckets.front() >= config.minimum_tensor_width &&
        tensor_width_buckets.back() == config.maximum_tensor_width &&
        std::all_of(tensor_width_buckets.begin(), tensor_width_buckets.end(),
                    [&](std::uint32_t width) {
                      return width <= config.maximum_tensor_width &&
                             width % tensor_width_multiple == 0;
                    });
    if (!valid_buckets) {
      return failure<RecognitionSample>(
          ErrorCode::invalid_argument,
          "Recognition width bucket contract is invalid");
    }
    const auto bucket = std::lower_bound(
        tensor_width_buckets.begin(), tensor_width_buckets.end(), tensor_width);
    if (bucket == tensor_width_buckets.end()) {
      return failure<RecognitionSample>(
          ErrorCode::resource_limit_exceeded,
          "Recognition width has no qualified bucket");
    }
    tensor_width = *bucket;
  }
  const auto content_width = std::min(
      tensor_width, static_cast<std::uint32_t>(std::ceil(config.height * ratio)));
  if (tensor_width > limits.max_recognition_width || content_width == 0) {
    return failure<RecognitionSample>(ErrorCode::resource_limit_exceeded,
                                      "Recognition tensor width exceeds limits");
  }
  return Result<RecognitionSample>::success(
      RecognitionSample{input_index, tensor_width, content_width});
}

}  // namespace

Result<DetectionInput> make_detection_input(const cv::Mat& bgr,
                                            const DetectionConfig& config,
                                            DetectionStrategy strategy,
                                            std::uint32_t max_side,
                                            const ResourceLimits& limits) {
  try {
    if (bgr.empty() || bgr.type() != CV_8UC3) {
      return failure<DetectionInput>(ErrorCode::invalid_image,
                                     "Detection input must be a non-empty BGR8 matrix");
    }
    const auto original_height = static_cast<std::uint32_t>(bgr.rows);
    const auto original_width = static_cast<std::uint32_t>(bgr.cols);
    cv::Mat padded;
    const cv::Mat* resize_source = &bgr;
    if (static_cast<std::uint64_t>(original_height) + original_width < 64) {
      const auto padded_height = std::max<std::uint32_t>(32, original_height);
      const auto padded_width = std::max<std::uint32_t>(32, original_width);
      padded = cv::Mat(static_cast<int>(padded_height), static_cast<int>(padded_width),
                       CV_8UC3, cv::Scalar(0, 0, 0));
      bgr.copyTo(padded(cv::Rect(0, 0, bgr.cols, bgr.rows)));
      resize_source = &padded;
    }
    const auto source_height = static_cast<std::uint32_t>(resize_source->rows);
    const auto source_width = static_cast<std::uint32_t>(resize_source->cols);
    if (max_side < config.minimum_dimension ||
        max_side > config.max_side_limit ||
        max_side > limits.max_detection_side ||
        (strategy == DetectionStrategy::bounded &&
         max_side % config.dimension_multiple != 0)) {
      return failure<DetectionInput>(
          ErrorCode::invalid_argument,
          "Detection strategy side is outside effective limits");
    }
    double ratio = 1.0;
    if (strategy == DetectionStrategy::bounded) {
      const auto shortest = std::min(source_height, source_width);
      const auto longest = std::max(source_height, source_width);
      if (shortest < config.limit_side_len) {
        ratio = static_cast<double>(config.limit_side_len) / shortest;
      }
      if (static_cast<double>(longest) * ratio > max_side) {
        ratio = static_cast<double>(max_side) / longest;
      }
    } else if (strategy == DetectionStrategy::upstream_exact) {
      const auto shortest = std::min(source_height, source_width);
      if (shortest < config.limit_side_len) {
        ratio = static_cast<double>(config.limit_side_len) / shortest;
      }
    } else {
      return failure<DetectionInput>(ErrorCode::invalid_argument,
                                     "Detection strategy is unsupported");
    }
    auto resized_height = static_cast<std::uint32_t>(source_height * ratio);
    auto resized_width = static_cast<std::uint32_t>(source_width * ratio);
    if (strategy == DetectionStrategy::upstream_exact &&
        std::max(resized_height, resized_width) > config.max_side_limit) {
      ratio = static_cast<double>(config.max_side_limit) /
              std::max(resized_height, resized_width);
      resized_height = static_cast<std::uint32_t>(resized_height * ratio);
      resized_width = static_cast<std::uint32_t>(resized_width * ratio);
    }
    const auto align_dimension = strategy == DetectionStrategy::bounded
                                     ? round_multiple_up
                                     : round_multiple_half_even;
    resized_height = std::max(
        config.minimum_dimension,
        align_dimension(resized_height, config.dimension_multiple));
    resized_width = std::max(
        config.minimum_dimension,
        align_dimension(resized_width, config.dimension_multiple));
    if (resized_height > max_side || resized_width > max_side ||
        resized_height > limits.max_detection_side ||
        resized_width > limits.max_detection_side) {
      return failure<DetectionInput>(ErrorCode::resource_limit_exceeded,
                                     "Detection tensor dimensions exceed the engine limit");
    }

    std::uint64_t pixels = 0;
    std::uint64_t elements = 0;
    std::uint64_t bytes = 0;
    std::uint64_t resized_bytes = 0;
    std::uint64_t padded_bytes = 0;
    std::uint64_t aggregate_bytes = 0;
    if (!checked_mul<std::uint64_t>(resized_height, resized_width, &pixels) ||
        !checked_mul<std::uint64_t>(pixels, 3, &elements) || !tensor_bytes(elements, &bytes) ||
        !checked_mul<std::uint64_t>(pixels, 3, &resized_bytes) ||
        (!padded.empty() &&
         !checked_mul<std::uint64_t>(padded.total(), padded.elemSize(), &padded_bytes)) ||
        !checked_add<std::uint64_t>(bytes, resized_bytes, &aggregate_bytes) ||
        !checked_add<std::uint64_t>(aggregate_bytes, padded_bytes, &aggregate_bytes) ||
        aggregate_bytes > limits.max_temporary_bytes ||
        elements > std::numeric_limits<std::size_t>::max()) {
      return failure<DetectionInput>(ErrorCode::resource_limit_exceeded,
                                     "Detection tensor exceeds the temporary memory limit");
    }

    cv::Mat resized;
    cv::resize(*resize_source, resized, cv::Size(static_cast<int>(resized_width),
                                                 static_cast<int>(resized_height)),
               0, 0, cv::INTER_LINEAR);
    DetectionInput result;
    result.values.resize(static_cast<std::size_t>(elements));
    result.shape = {1, 3, static_cast<std::int64_t>(resized_height),
                    static_cast<std::int64_t>(resized_width)};
    result.original_width = original_width;
    result.original_height = original_height;
    result.resized_width = resized_width;
    result.resized_height = resized_height;
    const std::size_t channel_size = static_cast<std::size_t>(pixels);
    for (std::uint32_t y = 0; y < resized_height; ++y) {
      const auto* row = resized.ptr<cv::Vec3b>(static_cast<int>(y));
      for (std::uint32_t x = 0; x < resized_width; ++x) {
        const auto offset = static_cast<std::size_t>(y) * resized_width + x;
        for (std::size_t channel = 0; channel < 3; ++channel) {
          result.values[channel * channel_size + offset] =
              (static_cast<float>(row[x][static_cast<int>(channel)]) * config.scale -
               config.mean[channel]) /
              config.std[channel];
        }
      }
    }
    return Result<DetectionInput>::success(std::move(result));
  } catch (const cv::Exception& exception) {
    return failure<DetectionInput>(ErrorCode::internal_error,
                                   "OpenCV failed during detection preprocessing", exception.err);
  } catch (const std::exception& exception) {
    return failure<DetectionInput>(ErrorCode::internal_error,
                                   "Unexpected detection preprocessing failure", exception.what());
  } catch (...) {
    return failure<DetectionInput>(ErrorCode::internal_error,
                                   "Unknown detection preprocessing failure");
  }
}

Result<std::vector<RecognitionBatchPlan>> plan_recognition_batches(
    const std::vector<Quad>& boxes, const GeometryConfig& geometry,
    const RecognitionConfig& config, std::uint32_t batch_size,
    const ResourceLimits& limits, std::uint32_t tensor_width_multiple,
    const std::vector<std::uint32_t>& tensor_width_buckets) {
  try {
    if (batch_size == 0 || batch_size > config.maximum_batch_size ||
        batch_size > limits.max_recognition_batch_size) {
      return failure<std::vector<RecognitionBatchPlan>>(
          ErrorCode::invalid_argument,
          "Recognition batch size is outside limits");
    }
    std::vector<RecognitionSample> samples;
    samples.reserve(boxes.size());
    for (std::size_t index = 0; index < boxes.size(); ++index) {
      auto shape_result = measure_text_region(boxes[index], geometry);
      if (!shape_result) {
        return Result<std::vector<RecognitionBatchPlan>>::failure(
            shape_result.error());
      }
      const auto shape = std::move(shape_result).value();
      auto sample_result = make_recognition_sample(
          index, shape.output_width(), shape.output_height(), config, limits,
          tensor_width_multiple, tensor_width_buckets);
      if (!sample_result) {
        return Result<std::vector<RecognitionBatchPlan>>::failure(
            sample_result.error());
      }
      samples.push_back(std::move(sample_result).value());
    }

    std::stable_sort(samples.begin(), samples.end(), [](const auto& left, const auto& right) {
      return left.tensor_width < right.tensor_width;
    });
    std::vector<RecognitionBatchPlan> plans;
    plans.reserve((samples.size() + batch_size - 1) / batch_size);
    for (std::size_t begin = 0; begin < samples.size(); begin += batch_size) {
      const auto end = std::min(samples.size(), begin + batch_size);
      RecognitionBatchPlan plan;
      plan.samples.assign(samples.begin() + static_cast<std::ptrdiff_t>(begin),
                          samples.begin() + static_cast<std::ptrdiff_t>(end));
      plans.push_back(std::move(plan));
    }
    return Result<std::vector<RecognitionBatchPlan>>::success(std::move(plans));
  } catch (const std::exception& exception) {
    return failure<std::vector<RecognitionBatchPlan>>(
        ErrorCode::internal_error,
        "Unexpected recognition batch planning failure", exception.what());
  } catch (...) {
    return failure<std::vector<RecognitionBatchPlan>>(
        ErrorCode::internal_error, "Unknown recognition batch planning failure");
  }
}

Result<RecognitionBatch> make_recognition_batch(
    const std::vector<cv::Mat>& crops, const RecognitionBatchPlan& plan,
    const RecognitionConfig& config, const ResourceLimits& limits,
    std::uint32_t tensor_width_multiple,
    const std::vector<std::uint32_t>& tensor_width_buckets) {
  try {
    const auto count = plan.samples.size();
    if (count == 0 || count != crops.size() ||
        count > config.maximum_batch_size ||
        count > limits.max_recognition_batch_size) {
      return failure<RecognitionBatch>(
          ErrorCode::invalid_argument,
          "Recognition batch plan is empty or outside limits");
    }

    std::uint32_t width = 0;
    std::uint32_t maximum_content_width = 0;
    for (std::size_t index = 0; index < count; ++index) {
      const auto& crop = crops[index];
      if (crop.empty() || crop.type() != CV_8UC3) {
        return failure<RecognitionBatch>(ErrorCode::postprocess_failed,
                                         "Recognition crop is not BGR8");
      }
      auto actual_result = make_recognition_sample(
          plan.samples[index].input_index,
          static_cast<std::uint32_t>(crop.cols),
          static_cast<std::uint32_t>(crop.rows), config, limits,
          tensor_width_multiple, tensor_width_buckets);
      if (!actual_result) {
        return Result<RecognitionBatch>::failure(actual_result.error());
      }
      const auto actual = std::move(actual_result).value();
      if (actual.tensor_width != plan.samples[index].tensor_width ||
          actual.content_width != plan.samples[index].content_width) {
        return failure<RecognitionBatch>(
            ErrorCode::postprocess_failed,
            "Recognition crop dimensions do not match the batch plan");
      }
      width = std::max(width, actual.tensor_width);
      maximum_content_width =
          std::max(maximum_content_width, actual.content_width);
    }

    std::uint64_t plane = 0;
    std::uint64_t batch_elements = 0;
    std::uint64_t batch_bytes = 0;
    std::uint64_t transient_pixels = 0;
    std::uint64_t transient_bytes = 0;
    std::uint64_t peak_bytes = 0;
    if (!checked_mul<std::uint64_t>(config.height, width, &plane) ||
        !checked_mul<std::uint64_t>(plane, 3, &batch_elements) ||
        !checked_mul<std::uint64_t>(batch_elements, count, &batch_elements) ||
        !tensor_bytes(batch_elements, &batch_bytes) ||
        !checked_mul<std::uint64_t>(config.height, maximum_content_width,
                                   &transient_pixels) ||
        !checked_mul<std::uint64_t>(transient_pixels, 3, &transient_bytes) ||
        !checked_add<std::uint64_t>(batch_bytes, transient_bytes, &peak_bytes) ||
        peak_bytes > limits.max_temporary_bytes ||
        batch_elements > std::numeric_limits<std::size_t>::max()) {
      return failure<RecognitionBatch>(ErrorCode::resource_limit_exceeded,
                                       "Recognition batch exceeds memory limits");
    }

    RecognitionBatch batch;
    batch.values.assign(static_cast<std::size_t>(batch_elements),
                        config.padding_value);
    batch.shape = {static_cast<std::int64_t>(count), 3,
                   static_cast<std::int64_t>(config.height),
                   static_cast<std::int64_t>(width)};
    batch.input_indices.reserve(count);
    const auto destination_plane = static_cast<std::size_t>(plane);
    for (std::size_t batch_index = 0; batch_index < count; ++batch_index) {
      const auto& sample = plan.samples[batch_index];
      batch.input_indices.push_back(sample.input_index);
      cv::Mat resized;
      cv::resize(crops[batch_index], resized,
                 cv::Size(static_cast<int>(sample.content_width),
                          static_cast<int>(config.height)),
                 0, 0, cv::INTER_LINEAR);
      for (std::uint32_t row_index = 0; row_index < config.height;
           ++row_index) {
        const auto* row = resized.ptr<cv::Vec3b>(static_cast<int>(row_index));
        for (std::uint32_t column = 0; column < sample.content_width;
             ++column) {
          const auto pixel_offset =
              static_cast<std::size_t>(row_index) * width + column;
          for (std::size_t channel = 0; channel < 3; ++channel) {
            const auto destination_offset =
                batch_index * 3 * destination_plane +
                channel * destination_plane + pixel_offset;
            batch.values[destination_offset] =
                (static_cast<float>(
                     row[column][static_cast<int>(channel)]) *
                     config.scale -
                 config.mean[channel]) /
                config.std[channel];
          }
        }
      }
    }
    return Result<RecognitionBatch>::success(std::move(batch));
  } catch (const cv::Exception& exception) {
    return failure<RecognitionBatch>(
        ErrorCode::postprocess_failed,
        "OpenCV failed during recognition preprocessing", exception.err);
  } catch (const std::exception& exception) {
    return failure<RecognitionBatch>(
        ErrorCode::internal_error,
        "Unexpected recognition preprocessing failure", exception.what());
  } catch (...) {
    return failure<RecognitionBatch>(ErrorCode::internal_error,
                                     "Unknown recognition preprocessing failure");
  }
}

}  // namespace light_ocr::internal
