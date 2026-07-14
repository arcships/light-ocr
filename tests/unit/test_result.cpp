#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "light_ocr/error.hpp"
#include "light_ocr/types.hpp"
#include "recognition/ctc_decode.hpp"
#include "result/assemble.hpp"
#include "test.hpp"

using namespace light_ocr;

namespace {

Quad box() {
  return Quad{{Point{1, 1}, Point{20, 1}, Point{20, 10}, Point{1, 10}}};
}

internal::DecodedText decoded(std::string text, float confidence) {
  internal::DecodedText result;
  result.text = std::move(text);
  result.confidence = confidence;
  return result;
}

}  // namespace

LIGHT_OCR_TEST(result_assembly_filters_lines_and_preserves_diagnostics) {
  auto result = internal::assemble_ocr_result(
      100, 50, "bundle", Timing{}, 7, {box(), box(), box()},
      {decoded("中文", 0.9f), decoded("low", 0.4f), decoded("", 0.0f)},
      0.5f, true);
  EXPECT_TRUE(result);
  EXPECT_EQ(result.value().lines.size(), 1u);
  EXPECT_EQ(result.value().lines[0].text, "中文");
  EXPECT_TRUE(result.value().diagnostics.has_value());
  EXPECT_EQ(result.value().diagnostics->detected_candidates, 7u);
  EXPECT_EQ(result.value().diagnostics->accepted_boxes, 3u);
  EXPECT_EQ(result.value().diagnostics->rejected_lines.size(), 2u);
  EXPECT_EQ(result.value().diagnostics->rejected_lines[0].reason,
            RejectionReason::below_score_threshold);
  EXPECT_EQ(result.value().diagnostics->rejected_lines[1].reason,
            RejectionReason::empty_decode);
}

LIGHT_OCR_TEST(result_assembly_rejects_invalid_utf8_confidence_and_geometry) {
  const std::string invalid_utf8("\xc0\x80", 2);
  auto text = internal::assemble_ocr_result(
      100, 50, "bundle", Timing{}, 1, {box()},
      {decoded(invalid_utf8, 0.9f)}, 0, false);
  EXPECT_FALSE(text);
  EXPECT_EQ(text.error().code, ErrorCode::postprocess_failed);

  auto confidence = internal::assemble_ocr_result(
      100, 50, "bundle", Timing{}, 1, {box()},
      {decoded("ok", std::numeric_limits<float>::quiet_NaN())}, 0, false);
  EXPECT_FALSE(confidence);
  EXPECT_EQ(confidence.error().code, ErrorCode::postprocess_failed);

  auto invalid_box = box();
  invalid_box.points[1].x = 101;
  auto geometry = internal::assemble_ocr_result(
      100, 50, "bundle", Timing{}, 1, {invalid_box},
      {decoded("ok", 0.9f)}, 0, false);
  EXPECT_FALSE(geometry);
  EXPECT_EQ(geometry.error().code, ErrorCode::postprocess_failed);

  auto concave_box = box();
  concave_box.points[2] = Point{5, 5};
  auto concave = internal::assemble_ocr_result(
      100, 50, "bundle", Timing{}, 1, {concave_box},
      {decoded("ok", 0.9f)}, 0, false);
  EXPECT_FALSE(concave);
  EXPECT_EQ(concave.error().code, ErrorCode::postprocess_failed);
}

LIGHT_OCR_TEST(result_assembly_rejects_mismatched_stage_counts) {
  auto result = internal::assemble_ocr_result(
      100, 50, "bundle", Timing{}, 0, {box()}, {}, 0, false);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::postprocess_failed);
}
