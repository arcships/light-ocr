#include <cstddef>
#include <cstdint>
#include <limits>

#include "test.hpp"
#include "util/checked_math.hpp"

using namespace light_ocr;

LIGHT_OCR_TEST(checked_arithmetic_accepts_boundary_values) {
  std::uint32_t value = 0;
  EXPECT_TRUE(internal::checked_add<std::uint32_t>(
      std::numeric_limits<std::uint32_t>::max() - 1, 1, &value));
  EXPECT_EQ(value, std::numeric_limits<std::uint32_t>::max());
  EXPECT_TRUE(internal::checked_mul<std::uint32_t>(0, 123, &value));
  EXPECT_EQ(value, 0u);
}

LIGHT_OCR_TEST(checked_arithmetic_rejects_overflow) {
  std::uint64_t value = 7;
  EXPECT_FALSE(internal::checked_add<std::uint64_t>(
      std::numeric_limits<std::uint64_t>::max(), 1, &value));
  EXPECT_FALSE(internal::checked_mul<std::uint64_t>(
      std::numeric_limits<std::uint64_t>::max(), 2, &value));
}

LIGHT_OCR_TEST(checked_image_bytes_honors_last_row_extent) {
  std::size_t value = 0;
  EXPECT_TRUE(internal::checked_image_bytes(3, 8, 6, &value));
  EXPECT_EQ(value, 22u);
  EXPECT_FALSE(internal::checked_image_bytes(0, 8, 6, &value));
  EXPECT_FALSE(internal::checked_image_bytes(
      std::numeric_limits<std::size_t>::max(), 8, 6, &value));
}
