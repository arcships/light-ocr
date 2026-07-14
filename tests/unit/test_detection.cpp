#include <cstdint>
#include <limits>
#include <vector>

#include "detection/db_postprocess.hpp"
#include "light_ocr/error.hpp"
#include "model/bundle_data.hpp"
#include "test.hpp"

using namespace light_ocr;

namespace {

internal::DetectionConfig detection_config() {
  internal::DetectionConfig value;
  value.threshold = 0.3f;
  value.box_threshold = 0.6f;
  value.unclip_ratio = 1.5f;
  value.max_candidates = 3000;
  value.use_dilation = false;
  value.score_mode = "fast";
  value.minimum_box_side = 3;
  return value;
}

}  // namespace

LIGHT_OCR_TEST(db_postprocess_finds_synthetic_rectangle) {
  constexpr std::uint32_t size = 64;
  std::vector<float> map(size * size, 0);
  for (std::uint32_t y = 20; y < 40; ++y) {
    for (std::uint32_t x = 10; x < 50; ++x) map[y * size + x] = 0.95f;
  }
  auto result = internal::db_postprocess(map.data(), map.size(), {1, 1, size, size}, size,
                                         size, detection_config(), ResourceLimits{});
  EXPECT_TRUE(result);
  EXPECT_EQ(result.value().contour_candidates, 1u);
  EXPECT_EQ(result.value().boxes.size(), 1u);
  EXPECT_TRUE(result.value().boxes[0].points[1].x > result.value().boxes[0].points[0].x);
}

LIGHT_OCR_TEST(db_postprocess_rejects_non_finite_map) {
  std::vector<float> map(16, 0);
  map[3] = std::numeric_limits<float>::infinity();
  auto result = internal::db_postprocess(map.data(), map.size(), {1, 1, 4, 4}, 4, 4,
                                         detection_config(), ResourceLimits{});
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::postprocess_failed);
}

LIGHT_OCR_TEST(db_postprocess_rejects_invalid_tensor_contract) {
  const std::vector<float> map(16, 0);
  auto rank = internal::db_postprocess(map.data(), map.size(), {4, 4}, 4, 4,
                                       detection_config(), ResourceLimits{});
  EXPECT_FALSE(rank);
  EXPECT_EQ(rank.error().code, ErrorCode::postprocess_failed);
  auto size = internal::db_postprocess(map.data(), map.size() - 1, {1, 1, 4, 4}, 4, 4,
                                       detection_config(), ResourceLimits{});
  EXPECT_FALSE(size);
  EXPECT_EQ(size.error().code, ErrorCode::postprocess_failed);
}

LIGHT_OCR_TEST(db_postprocess_enforces_candidate_limit) {
  constexpr std::uint32_t size = 64;
  std::vector<float> map(size * size, 0);
  for (std::uint32_t y = 4; y < 12; ++y) {
    for (std::uint32_t x = 4; x < 12; ++x) map[y * size + x] = 0.95f;
  }
  for (std::uint32_t y = 40; y < 48; ++y) {
    for (std::uint32_t x = 40; x < 48; ++x) map[y * size + x] = 0.95f;
  }
  auto config = detection_config();
  config.max_candidates = 1;
  auto result = internal::db_postprocess(map.data(), map.size(), {1, 1, size, size}, size,
                                         size, config, ResourceLimits{});
  EXPECT_TRUE(result);
  EXPECT_EQ(result.value().contour_candidates, 1u);
  EXPECT_TRUE(result.value().boxes.size() <= 1u);
}
