#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "light_ocr/error.hpp"
#include "light_ocr/types.hpp"

namespace light_ocr::node {

struct DecodedImage {
  std::vector<std::uint8_t> bytes;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::size_t stride = 0;
  PixelFormat pixel_format = PixelFormat::rgb8;
};

Result<DecodedImage> decode_encoded_image(
    const std::vector<std::uint8_t>& encoded,
    const ResourceLimits& limits) noexcept;

}  // namespace light_ocr::node
