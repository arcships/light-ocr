#include <limits>
#include <vector>

#include <opencv2/core.hpp>

#include "geometry/geometry.hpp"
#include "light_ocr/types.hpp"
#include "model/bundle_data.hpp"
#include "test.hpp"

using namespace light_ocr;

LIGHT_OCR_TEST(reading_order_bubbles_boxes_within_row_band) {
  Quad right{{Point{50, 10}, Point{80, 10}, Point{80, 20}, Point{50, 20}}};
  Quad left{{Point{5, 15}, Point{35, 15}, Point{35, 25}, Point{5, 25}}};
  internal::GeometryConfig config{10, 1.5f};
  auto sorted = internal::sort_reading_order({right, left}, config);
  EXPECT_EQ(sorted[0].points[0].x, 5);
  EXPECT_EQ(sorted[1].points[0].x, 50);
}

LIGHT_OCR_TEST(tall_crop_rotates_counterclockwise) {
  cv::Mat image(100, 100, CV_8UC3, cv::Scalar(1, 2, 3));
  Quad tall{{Point{20, 10}, Point{40, 10}, Point{40, 90}, Point{20, 90}}};
  internal::GeometryConfig config{10, 1.5f};
  auto crops = internal::crop_text_regions(image, {tall}, config, ResourceLimits{});
  EXPECT_TRUE(crops);
  EXPECT_EQ(crops.value().size(), 1u);
  EXPECT_TRUE(crops.value()[0].cols > crops.value()[0].rows);
}

LIGHT_OCR_TEST(crop_rejects_degenerate_and_memory_limited_regions) {
  cv::Mat image(100, 100, CV_8UC3, cv::Scalar(1, 2, 3));
  internal::GeometryConfig config{10, 1.5f};
  Quad point{{Point{10, 10}, Point{10, 10}, Point{10, 10}, Point{10, 10}}};
  auto degenerate = internal::crop_text_regions(image, {point}, config, ResourceLimits{});
  EXPECT_FALSE(degenerate);
  EXPECT_EQ(degenerate.error().code, ErrorCode::postprocess_failed);

  Quad concave{{Point{10, 10}, Point{90, 10}, Point{40, 40}, Point{10, 90}}};
  auto concave_result =
      internal::crop_text_regions(image, {concave}, config, ResourceLimits{});
  EXPECT_FALSE(concave_result);
  EXPECT_EQ(concave_result.error().code, ErrorCode::postprocess_failed);

  Quad region{{Point{10, 10}, Point{90, 10}, Point{90, 90}, Point{10, 90}}};
  ResourceLimits limits;
  limits.max_temporary_bytes = 1;
  auto memory_limited = internal::crop_text_regions(image, {region}, config, limits);
  EXPECT_FALSE(memory_limited);
  EXPECT_EQ(memory_limited.error().code, ErrorCode::resource_limit_exceeded);
}

LIGHT_OCR_TEST(crop_preserves_the_supplied_perspective_quad) {
  cv::Mat image(100, 120, CV_8UC3, cv::Scalar(1, 2, 3));
  Quad perspective{{Point{10, 10}, Point{90, 20}, Point{80, 60}, Point{20, 50}}};
  internal::GeometryConfig config{10, 1.5f};
  auto crops = internal::crop_text_regions(image, {perspective}, config, ResourceLimits{});
  EXPECT_TRUE(crops);
  EXPECT_EQ(crops.value().size(), 1u);
  EXPECT_EQ(crops.value()[0].cols, 80);
  EXPECT_EQ(crops.value()[0].rows, 41);
}

LIGHT_OCR_TEST(crop_rejects_finite_but_unrepresentable_dimensions) {
  cv::Mat image(10, 10, CV_8UC3, cv::Scalar(1, 2, 3));
  const auto maximum = std::numeric_limits<float>::max();
  Quad oversized{{Point{-maximum, 0}, Point{maximum, 0}, Point{maximum, 1},
                  Point{-maximum, 1}}};
  internal::GeometryConfig config{10, 1.5f};
  auto result =
      internal::crop_text_regions(image, {oversized}, config, ResourceLimits{});
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::resource_limit_exceeded);
}
