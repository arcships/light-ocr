#include <cstddef>
#include <cstdint>

#include "light_ocr/types.hpp"
#include "preprocess/image.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size < 8) return 0;
  const auto width = static_cast<std::uint32_t>(data[0]) + 1;
  const auto height = static_cast<std::uint32_t>(data[1]) + 1;
  const auto format = static_cast<light_ocr::PixelFormat>(data[2] % 6);
  const auto stride = static_cast<std::size_t>(data[3]) |
                      (static_cast<std::size_t>(data[4]) << 8);
  light_ocr::ResourceLimits limits;
  limits.max_width = static_cast<std::uint32_t>(data[5]) + 1;
  limits.max_height = static_cast<std::uint32_t>(data[6]) + 1;
  limits.max_pixels = static_cast<std::uint64_t>(data[7]) * 1024 + 1;
  const light_ocr::ImageView image{data + 8, size - 8, width, height, stride, format};
  (void)light_ocr::internal::validate_and_convert_image(image, limits);
  return 0;
}
