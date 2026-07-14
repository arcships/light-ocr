#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace light_ocr::internal {

std::string sha256_hex(const std::uint8_t* data, std::size_t size);

}  // namespace light_ocr::internal
