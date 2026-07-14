#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "light_ocr/error.hpp"
#include "light_ocr/types.hpp"
#include "recognition/ctc_decode.hpp"

namespace light_ocr::internal {

bool valid_utf8(std::string_view value) noexcept;

Result<OcrResult> assemble_ocr_result(
    std::uint32_t image_width, std::uint32_t image_height,
    std::string model_bundle_id, Timing timing,
    std::uint32_t detected_candidates, std::vector<Quad> boxes,
    std::vector<DecodedText> decoded, float score_threshold,
    bool include_diagnostics) noexcept;

}  // namespace light_ocr::internal
