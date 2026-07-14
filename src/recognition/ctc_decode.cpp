#include "recognition/ctc_decode.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "util/checked_math.hpp"

namespace light_ocr::internal {

Result<std::vector<DecodedText>> decode_ctc(const float* data, std::size_t element_count,
                                            const std::vector<std::int64_t>& shape,
                                            const std::vector<std::string>& characters,
                                            std::uint32_t blank_index, bool collapse_repeats) {
  try {
    if (data == nullptr || shape.size() != 3 || shape[0] <= 0 || shape[1] <= 0 ||
        shape[2] <= 0) {
      return Result<std::vector<DecodedText>>::failure(
          Error{ErrorCode::postprocess_failed, "Recognition output must be a non-empty rank-3 tensor", {}});
    }
    const auto batch = static_cast<std::uint64_t>(shape[0]);
    const auto time_steps = static_cast<std::uint64_t>(shape[1]);
    const auto classes = static_cast<std::uint64_t>(shape[2]);
    std::uint64_t expected = 0;
    if (!checked_mul(batch, time_steps, &expected) || !checked_mul(expected, classes, &expected) ||
        expected != element_count) {
      return Result<std::vector<DecodedText>>::failure(
          Error{ErrorCode::postprocess_failed, "Recognition output element count does not match shape", {}});
    }
    if (blank_index != 0 || classes != characters.size() + 1) {
      return Result<std::vector<DecodedText>>::failure(
          Error{ErrorCode::postprocess_failed, "Recognition class count does not match dictionary", {}});
    }

    std::vector<DecodedText> results;
    results.reserve(static_cast<std::size_t>(batch));
    for (std::uint64_t sample = 0; sample < batch; ++sample) {
      DecodedText decoded;
      std::uint32_t previous = std::numeric_limits<std::uint32_t>::max();
      for (std::uint64_t step = 0; step < time_steps; ++step) {
        const auto offset = static_cast<std::size_t>((sample * time_steps + step) * classes);
        std::uint32_t best_index = 0;
        float best_value = data[offset];
        if (!std::isfinite(best_value)) {
          return Result<std::vector<DecodedText>>::failure(
              Error{ErrorCode::postprocess_failed, "Recognition output contains a non-finite value", {}});
        }
        for (std::uint32_t class_index = 1; class_index < classes; ++class_index) {
          const auto value = data[offset + class_index];
          if (!std::isfinite(value)) {
            return Result<std::vector<DecodedText>>::failure(
                Error{ErrorCode::postprocess_failed, "Recognition output contains a non-finite value", {}});
          }
          if (value > best_value) {
            best_value = value;
            best_index = class_index;
          }
        }
        if (best_index != blank_index && (!collapse_repeats || best_index != previous)) {
          const auto character_index = best_index - 1;
          if (character_index >= characters.size()) {
            return Result<std::vector<DecodedText>>::failure(
                Error{ErrorCode::postprocess_failed, "Recognition class index is outside dictionary", {}});
          }
          decoded.text += characters[character_index];
          decoded.selected_indices.push_back(best_index);
          decoded.selected_probabilities.push_back(best_value);
        }
        previous = best_index;
      }
      if (!decoded.selected_probabilities.empty()) {
        double total = 0;
        for (const auto value : decoded.selected_probabilities) total += value;
        decoded.confidence =
            static_cast<float>(total / static_cast<double>(decoded.selected_probabilities.size()));
      }
      results.push_back(std::move(decoded));
    }
    return Result<std::vector<DecodedText>>::success(std::move(results));
  } catch (const std::exception& exception) {
    return Result<std::vector<DecodedText>>::failure(
        Error{ErrorCode::internal_error, "Unexpected CTC decoding failure", exception.what()});
  } catch (...) {
    return Result<std::vector<DecodedText>>::failure(
        Error{ErrorCode::internal_error, "Unknown CTC decoding failure", {}});
  }
}

}  // namespace light_ocr::internal
