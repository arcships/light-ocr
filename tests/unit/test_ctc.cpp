#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "light_ocr/error.hpp"
#include "recognition/ctc_decode.hpp"
#include "test.hpp"

using light_ocr::ErrorCode;
using light_ocr::internal::decode_ctc;

LIGHT_OCR_TEST(ctc_removes_blank_and_consecutive_duplicates) {
  const std::vector<std::string> dictionary = {"a", " "};
  const std::vector<float> output = {
      0.9f, 0.1f, 0.0f, 0.1f, 0.8f, 0.1f, 0.1f, 0.7f, 0.2f,
      0.8f, 0.1f, 0.1f, 0.1f, 0.6f, 0.3f, 0.1f, 0.2f, 0.9f,
  };
  auto result = decode_ctc(output.data(), output.size(), {1, 6, 3}, dictionary, 0, true);
  EXPECT_TRUE(result);
  EXPECT_EQ(result.value().size(), 1u);
  EXPECT_EQ(result.value()[0].text, "aa ");
  EXPECT_EQ(result.value()[0].selected_indices.size(), 3u);
  EXPECT_NEAR(result.value()[0].confidence, (0.8 + 0.6 + 0.9) / 3.0, 1e-6);
}

LIGHT_OCR_TEST(ctc_rejects_dictionary_class_mismatch) {
  const std::vector<float> output = {1, 0, 0};
  auto result = decode_ctc(output.data(), output.size(), {1, 1, 3}, {"a"}, 0, true);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::postprocess_failed);
}

LIGHT_OCR_TEST(ctc_rejects_non_finite_output) {
  const std::vector<float> output = {0, std::numeric_limits<float>::quiet_NaN()};
  auto result = decode_ctc(output.data(), output.size(), {1, 1, 2}, {"a"}, 0, true);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::postprocess_failed);
}

LIGHT_OCR_TEST(ctc_constructs_utf8_without_byte_reinterpretation) {
  const std::vector<std::string> dictionary = {"中", "あ", "繁"};
  const std::vector<float> output = {
      0.0f, 0.9f, 0.1f, 0.0f,
      0.0f, 0.1f, 0.8f, 0.1f,
      0.9f, 0.0f, 0.0f, 0.1f,
      0.0f, 0.1f, 0.0f, 0.9f,
  };
  auto result = decode_ctc(output.data(), output.size(), {1, 4, 4}, dictionary, 0, true);
  EXPECT_TRUE(result);
  EXPECT_EQ(result.value()[0].text, "中あ繁");
  EXPECT_NEAR(result.value()[0].confidence, (0.9 + 0.8 + 0.9) / 3.0, 1e-6);
}

LIGHT_OCR_TEST(ctc_rejects_invalid_rank_and_storage_size) {
  const std::vector<float> output = {0.9f, 0.1f};
  auto rank = decode_ctc(output.data(), output.size(), {1, 2}, {"a"}, 0, true);
  EXPECT_FALSE(rank);
  EXPECT_EQ(rank.error().code, ErrorCode::postprocess_failed);
  auto storage = decode_ctc(output.data(), 1, {1, 1, 2}, {"a"}, 0, true);
  EXPECT_FALSE(storage);
  EXPECT_EQ(storage.error().code, ErrorCode::postprocess_failed);
}
