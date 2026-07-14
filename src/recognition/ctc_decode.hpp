#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "light_ocr/error.hpp"

namespace light_ocr::internal {

struct DecodedText {
  std::string text;
  float confidence = 0;
  std::vector<std::uint32_t> selected_indices;
  std::vector<float> selected_probabilities;
};

Result<std::vector<DecodedText>> decode_ctc(const float* data, std::size_t element_count,
                                            const std::vector<std::int64_t>& shape,
                                            const std::vector<std::string>& characters,
                                            std::uint32_t blank_index, bool collapse_repeats);

}  // namespace light_ocr::internal
