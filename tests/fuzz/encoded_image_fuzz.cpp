#include <cstddef>
#include <cstdint>
#include <vector>

#include "encoded_image.hpp"
#include "light_ocr/types.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  std::vector<std::uint8_t> encoded;
  if (size != 0) encoded.assign(data, data + size);
  light_ocr::ResourceLimits limits;
  limits.max_width = 4096;
  limits.max_height = 4096;
  limits.max_pixels = 16ull * 1024 * 1024;
  limits.max_temporary_bytes = 128ull * 1024 * 1024;
  (void)light_ocr::node::decode_encoded_image(encoded, limits);
  return 0;
}
