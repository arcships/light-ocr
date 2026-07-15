#include <cstdint>
#include <vector>

#include <opencv2/core.hpp>

#include "light_ocr/error.hpp"
#include "light_ocr/types.hpp"
#include "model/bundle_data.hpp"
#include "preprocess/image.hpp"
#include "preprocess/tensor.hpp"
#include "test.hpp"

using namespace light_ocr;

namespace {

internal::RecognitionConfig recognition_config() {
  internal::RecognitionConfig config;
  config.channels = 3;
  config.height = 48;
  config.base_width = 320;
  config.minimum_tensor_width = 320;
  config.maximum_tensor_width = 3200;
  config.scale = 1.0f / 255.0f;
  config.mean = {0.5f, 0.5f, 0.5f};
  config.std = {0.5f, 0.5f, 0.5f};
  config.padding_value = 0;
  config.maximum_batch_size = 8;
  return config;
}

Quad rectangle(float width, float height) {
  return Quad{{Point{0, 0}, Point{width, 0}, Point{width, height},
               Point{0, height}}};
}

}  // namespace

LIGHT_OCR_TEST(image_rejects_bad_stride_and_truncated_buffer) {
  const std::vector<std::uint8_t> bad_stride_bytes(256, 0);
  const ImageView bad_stride{bad_stride_bytes.data(), bad_stride_bytes.size(), 16, 16, 15,
                             PixelFormat::gray8};
  auto bad_stride_result = internal::validate_and_convert_image(bad_stride, ResourceLimits{});
  EXPECT_FALSE(bad_stride_result);
  EXPECT_EQ(bad_stride_result.error().code, ErrorCode::invalid_image);

  const std::vector<std::uint8_t> truncated_bytes(767, 0);
  const ImageView truncated{truncated_bytes.data(), truncated_bytes.size(), 16, 16, 48,
                            PixelFormat::bgr8};
  auto truncated_result = internal::validate_and_convert_image(truncated, ResourceLimits{});
  EXPECT_FALSE(truncated_result);
  EXPECT_EQ(truncated_result.error().code, ErrorCode::invalid_image);
}

LIGHT_OCR_TEST(image_rejects_null_empty_and_unknown_format) {
  ResourceLimits limits;
  auto empty = internal::validate_and_convert_image(ImageView{}, limits);
  EXPECT_FALSE(empty);
  EXPECT_EQ(empty.error().code, ErrorCode::invalid_image);

  const std::vector<std::uint8_t> pixel(3, 0);
  auto unsupported = internal::validate_and_convert_image(
      ImageView{pixel.data(), pixel.size(), 1, 1, 3, static_cast<PixelFormat>(99)}, limits);
  EXPECT_FALSE(unsupported);
  EXPECT_EQ(unsupported.error().code, ErrorCode::unsupported_pixel_format);
}

LIGHT_OCR_TEST(image_distinguishes_structural_errors_from_resource_limits) {
  const std::vector<std::uint8_t> pixels(12, 0);
  ResourceLimits limits;
  limits.max_width = 1;
  auto oversized = internal::validate_and_convert_image(
      ImageView{pixels.data(), pixels.size(), 2, 2, 6, PixelFormat::bgr8}, limits);
  EXPECT_FALSE(oversized);
  EXPECT_EQ(oversized.error().code, ErrorCode::resource_limit_exceeded);

  limits = ResourceLimits{};
  limits.max_temporary_bytes = 11;
  auto memory_limited = internal::validate_and_convert_image(
      ImageView{pixels.data(), pixels.size(), 2, 2, 6, PixelFormat::bgr8}, limits);
  EXPECT_FALSE(memory_limited);
  EXPECT_EQ(memory_limited.error().code, ErrorCode::resource_limit_exceeded);
}

LIGHT_OCR_TEST(image_layout_accepts_declared_maximum_gray8_contract) {
  const std::vector<std::uint8_t> pixels(40'000'000, 0);
  const ImageView maximum{pixels.data(), pixels.size(), 10'000, 4'000, 10'000,
                          PixelFormat::gray8};
  auto result = internal::validate_image_layout(maximum, ResourceLimits{});
  EXPECT_TRUE(result);
  EXPECT_EQ(result.value().required_bytes, 40'000'000u);
  EXPECT_EQ(result.value().converted_bytes, 120'000'000u);
}

LIGHT_OCR_TEST(image_converts_rgb_to_bgr_and_ignores_alpha) {
  const std::vector<std::uint8_t> rgb = {10, 20, 30};
  auto rgb_result = internal::validate_and_convert_image(
      ImageView{rgb.data(), rgb.size(), 1, 1, 3, PixelFormat::rgb8}, ResourceLimits{});
  EXPECT_TRUE(rgb_result);
  const auto rgb_pixel = rgb_result.value().bgr.at<cv::Vec3b>(0, 0);
  EXPECT_EQ(rgb_pixel[0], 30);
  EXPECT_EQ(rgb_pixel[1], 20);
  EXPECT_EQ(rgb_pixel[2], 10);

  const std::vector<std::uint8_t> rgba = {1, 2, 3, 0};
  auto rgba_result = internal::validate_and_convert_image(
      ImageView{rgba.data(), rgba.size(), 1, 1, 4, PixelFormat::rgba8}, ResourceLimits{});
  EXPECT_TRUE(rgba_result);
  const auto rgba_pixel = rgba_result.value().bgr.at<cv::Vec3b>(0, 0);
  EXPECT_EQ(rgba_pixel[0], 3);
  EXPECT_EQ(rgba_pixel[1], 2);
  EXPECT_EQ(rgba_pixel[2], 1);
}

LIGHT_OCR_TEST(detection_resize_pads_tiny_images_before_min_64_resize) {
  cv::Mat image(10, 20, CV_8UC3, cv::Scalar(255, 255, 255));
  internal::DetectionConfig config;
  config.limit_side_len = 64;
  config.limit_type = "min";
  config.max_side_limit = 4000;
  config.dimension_multiple = 32;
  config.minimum_dimension = 32;
  config.scale = 1.0f / 255.0f;
  config.mean = {0.485f, 0.456f, 0.406f};
  config.std = {0.229f, 0.224f, 0.225f};
  auto result = internal::make_detection_input(
      image, config, DetectionStrategy::upstream_exact, 4000,
      ResourceLimits{});
  EXPECT_TRUE(result);
  EXPECT_EQ(result.value().shape[2], 64);
  EXPECT_EQ(result.value().shape[3], 64);
  EXPECT_NEAR(result.value().values[0], (1.0 - 0.485) / 0.229, 1e-5);
  const auto bottom_right = 64u * 64u - 1;
  EXPECT_NEAR(result.value().values[bottom_right], (0.0 - 0.485) / 0.229, 1e-5);
}

LIGHT_OCR_TEST(detection_preprocess_counts_image_workspace_and_tensor_memory) {
  cv::Mat image(32, 32, CV_8UC3, cv::Scalar(255, 255, 255));
  internal::DetectionConfig config;
  config.limit_side_len = 64;
  config.limit_type = "min";
  config.max_side_limit = 4000;
  config.dimension_multiple = 32;
  config.minimum_dimension = 32;
  config.scale = 1.0f / 255.0f;
  config.mean = {0.485f, 0.456f, 0.406f};
  config.std = {0.229f, 0.224f, 0.225f};
  ResourceLimits limits;
  limits.max_temporary_bytes = 64 * 64 * 3 * sizeof(float);
  auto result = internal::make_detection_input(
      image, config, DetectionStrategy::upstream_exact, 4000, limits);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::resource_limit_exceeded);
}

LIGHT_OCR_TEST(detection_bounded_limits_longest_side_and_preserves_minimum_short_side) {
  internal::DetectionConfig config;
  config.limit_side_len = 64;
  config.limit_type = "min";
  config.max_side_limit = 4000;
  config.dimension_multiple = 32;
  config.minimum_dimension = 32;
  config.scale = 1.0f / 255.0f;
  config.mean = {0.485f, 0.456f, 0.406f};
  config.std = {0.229f, 0.224f, 0.225f};

  cv::Mat large(1024, 2048, CV_8UC3, cv::Scalar(255, 255, 255));
  auto large_result = internal::make_detection_input(
      large, config, DetectionStrategy::bounded, 960, ResourceLimits{});
  EXPECT_TRUE(large_result);
  EXPECT_EQ(large_result.value().shape[2], 480);
  EXPECT_EQ(large_result.value().shape[3], 960);

  cv::Mat shallow(190, 1280, CV_8UC3, cv::Scalar(255, 255, 255));
  auto shallow_result = internal::make_detection_input(
      shallow, config, DetectionStrategy::bounded, 960, ResourceLimits{});
  EXPECT_TRUE(shallow_result);
  EXPECT_EQ(shallow_result.value().shape[2], 160);
  EXPECT_EQ(shallow_result.value().shape[3], 960);

  cv::Mat small(10, 20, CV_8UC3, cv::Scalar(255, 255, 255));
  auto small_result = internal::make_detection_input(
      small, config, DetectionStrategy::bounded, 960, ResourceLimits{});
  EXPECT_TRUE(small_result);
  EXPECT_EQ(small_result.value().shape[2], 64);
  EXPECT_EQ(small_result.value().shape[3], 64);
}

LIGHT_OCR_TEST(recognition_batches_restore_input_indices) {
  std::vector<cv::Mat> crops;
  crops.emplace_back(48, 480, CV_8UC3, cv::Scalar(0, 0, 0));
  crops.emplace_back(48, 96, CV_8UC3, cv::Scalar(255, 255, 255));
  const auto config = recognition_config();
  const std::vector<Quad> boxes{rectangle(480, 48), rectangle(96, 48)};
  internal::GeometryConfig geometry;
  geometry.tall_line_ratio = 1.5f;
  auto plans = internal::plan_recognition_batches(
      boxes, geometry, config, 2, ResourceLimits{});
  EXPECT_TRUE(plans);
  EXPECT_EQ(plans.value().size(), 1u);
  EXPECT_EQ(plans.value()[0].samples[0].input_index, 1u);
  EXPECT_EQ(plans.value()[0].samples[1].input_index, 0u);
  std::vector<cv::Mat> batch_crops{crops[1], crops[0]};
  auto batch = internal::make_recognition_batch(
      batch_crops, plans.value()[0], config, ResourceLimits{});
  EXPECT_TRUE(batch);
  EXPECT_EQ(batch.value().input_indices[0], 1u);
  EXPECT_EQ(batch.value().input_indices[1], 0u);
  EXPECT_EQ(batch.value().shape[3], 480);
}

LIGHT_OCR_TEST(recognition_batches_apply_and_validate_runtime_width_buckets) {
  const auto config = recognition_config();
  internal::GeometryConfig geometry;
  geometry.tall_line_ratio = 1.5f;
  const std::vector<Quad> boxes{rectangle(416, 48)};
  const std::vector<std::uint32_t> buckets{320, 480, 3200};
  auto plans = internal::plan_recognition_batches(
      boxes, geometry, config, 1, ResourceLimits{}, 32, buckets);
  EXPECT_TRUE(plans);
  EXPECT_EQ(plans.value()[0].samples[0].tensor_width, 480u);
  std::vector<cv::Mat> crops{
      cv::Mat(48, 416, CV_8UC3, cv::Scalar(0, 0, 0))};
  auto batch = internal::make_recognition_batch(
      crops, plans.value()[0], config, ResourceLimits{}, 32, buckets);
  EXPECT_TRUE(batch);
  EXPECT_EQ(batch.value().shape[3], 480);

  const std::vector<std::uint32_t> unsorted{480, 320, 3200};
  auto invalid = internal::plan_recognition_batches(
      boxes, geometry, config, 1, ResourceLimits{}, 32, unsorted);
  EXPECT_FALSE(invalid);
  EXPECT_EQ(invalid.error().code, ErrorCode::invalid_argument);
}

LIGHT_OCR_TEST(recognition_batches_reject_invalid_batch_and_memory_limit) {
  std::vector<cv::Mat> crops{cv::Mat(48, 320, CV_8UC3, cv::Scalar(0, 0, 0))};
  const auto config = recognition_config();
  internal::GeometryConfig geometry;
  geometry.tall_line_ratio = 1.5f;
  std::vector<Quad> boxes{rectangle(320, 48)};

  auto zero_batch = internal::plan_recognition_batches(
      boxes, geometry, config, 0, ResourceLimits{});
  EXPECT_FALSE(zero_batch);
  EXPECT_EQ(zero_batch.error().code, ErrorCode::invalid_argument);

  auto plans = internal::plan_recognition_batches(
      boxes, geometry, config, 1, ResourceLimits{});
  EXPECT_TRUE(plans);
  ResourceLimits limits;
  limits.max_temporary_bytes = 1;
  auto memory_limited = internal::make_recognition_batch(
      crops, plans.value()[0], config, limits);
  EXPECT_FALSE(memory_limited);
  EXPECT_EQ(memory_limited.error().code, ErrorCode::resource_limit_exceeded);

  crops.push_back(crops.front().clone());
  boxes.push_back(boxes.front());
  limits.max_temporary_bytes = 250'000;
  auto streaming_plans = internal::plan_recognition_batches(
      boxes, geometry, config, 1, limits);
  EXPECT_TRUE(streaming_plans);
  EXPECT_EQ(streaming_plans.value().size(), 2u);
  for (const auto& plan : streaming_plans.value()) {
    std::vector<cv::Mat> one_crop{crops[plan.samples[0].input_index]};
    auto batch = internal::make_recognition_batch(one_crop, plan, config, limits);
    EXPECT_TRUE(batch);
  }

  crops.resize(1);
  limits.max_temporary_bytes = 48 * 320 * 3 * sizeof(float);
  auto transient_limited = internal::make_recognition_batch(
      crops, plans.value()[0], config, limits);
  EXPECT_FALSE(transient_limited);
  EXPECT_EQ(transient_limited.error().code, ErrorCode::resource_limit_exceeded);
}
