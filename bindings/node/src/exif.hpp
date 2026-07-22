#pragma once

#include <cstdint>
#include <vector>

#include "light_ocr/error.hpp"
#include "encoded_image.hpp"

namespace light_ocr::node {
namespace exif {

// Parse the EXIF orientation tag (0x0112) from a JPEG buffer.
// Returns 1..8 if found, or 1 (normal) if not present or not a JPEG.
std::uint16_t parse_orientation(const std::vector<std::uint8_t>& encoded) noexcept;

// Apply EXIF orientation to decoded RGB pixel data.
// Returns the transformed image (dimensions may swap for 90/270 rotations).
DecodedImage apply_orientation(DecodedImage image, std::uint16_t orientation) noexcept;

}  // namespace exif
}  // namespace light_ocr::node
