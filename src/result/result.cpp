#include "light_ocr/error.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "result/assemble.hpp"

namespace light_ocr {

const char* to_string(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::invalid_argument: return "invalid_argument";
    case ErrorCode::invalid_image: return "invalid_image";
    case ErrorCode::unsupported_pixel_format: return "unsupported_pixel_format";
    case ErrorCode::unsupported_capability: return "unsupported_capability";
    case ErrorCode::invalid_model_bundle: return "invalid_model_bundle";
    case ErrorCode::unsupported_model: return "unsupported_model";
    case ErrorCode::model_integrity_failed: return "model_integrity_failed";
    case ErrorCode::runtime_initialization_failed: return "runtime_initialization_failed";
    case ErrorCode::inference_failed: return "inference_failed";
    case ErrorCode::postprocess_failed: return "postprocess_failed";
    case ErrorCode::resource_limit_exceeded: return "resource_limit_exceeded";
    case ErrorCode::invalid_engine: return "invalid_engine";
    case ErrorCode::internal_error: return "internal_error";
  }
  return "internal_error";
}

const char* to_string(CreationReason reason) noexcept {
  switch (reason) {
    case CreationReason::adapter_unavailable: return "adapter_unavailable";
    case CreationReason::model_compute_unsupported: return "model_compute_unsupported";
    case CreationReason::device_memory_insufficient: return "device_memory_insufficient";
    case CreationReason::driver_version_unsupported: return "driver_version_unsupported";
    case CreationReason::package_corrupt: return "package_corrupt";
    case CreationReason::artifact_hash_mismatch: return "artifact_hash_mismatch";
    case CreationReason::provider_abi_mismatch: return "provider_abi_mismatch";
    case CreationReason::internal_assertion_failed: return "internal_assertion_failed";
    case CreationReason::unrecoverable_load_failed: return "unrecoverable_load_failed";
  }
  return "internal_assertion_failed";
}

const char* to_string(CreationAttemptStatus status) noexcept {
  switch (status) {
    case CreationAttemptStatus::selected: return "selected";
    case CreationAttemptStatus::skipped: return "skipped";
    case CreationAttemptStatus::fatal: return "fatal";
  }
  return "fatal";
}

}  // namespace light_ocr

namespace light_ocr::internal {
namespace {

Result<OcrResult> assembly_failure(const char* message) {
  return Result<OcrResult>::failure(
      Error{ErrorCode::postprocess_failed, message, {}});
}

bool valid_quad(const Quad& quad, std::uint32_t width, std::uint32_t height) {
  double twice_signed_area = 0;
  for (std::size_t index = 0; index < quad.points.size(); ++index) {
    const auto& point = quad.points[index];
    const auto& next = quad.points[(index + 1) % quad.points.size()];
    const auto& following = quad.points[(index + 2) % quad.points.size()];
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.x < 0 ||
        point.y < 0 || point.x > width || point.y > height) {
      return false;
    }
    twice_signed_area += static_cast<double>(point.x) * next.y -
                         static_cast<double>(next.x) * point.y;
    const auto cross =
        (static_cast<double>(next.x) - point.x) *
            (static_cast<double>(following.y) - next.y) -
        (static_cast<double>(next.y) - point.y) *
            (static_cast<double>(following.x) - next.x);
    if (!std::isfinite(cross) || cross <= 0) return false;
  }
  return std::isfinite(twice_signed_area) && twice_signed_area > 0;
}

bool continuation(std::uint8_t value) { return value >= 0x80 && value <= 0xbf; }

}  // namespace

bool valid_utf8(std::string_view value) noexcept {
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(value.data());
  std::size_t index = 0;
  while (index < value.size()) {
    const auto first = bytes[index++];
    if (first <= 0x7f) continue;
    if (first >= 0xc2 && first <= 0xdf) {
      if (index >= value.size() || !continuation(bytes[index])) return false;
      ++index;
      continue;
    }
    if (first >= 0xe0 && first <= 0xef) {
      if (index + 1 >= value.size()) return false;
      const auto second = bytes[index];
      const auto third = bytes[index + 1];
      if (!continuation(third) ||
          (first == 0xe0 ? second < 0xa0 || second > 0xbf
                         : first == 0xed ? second < 0x80 || second > 0x9f
                                         : !continuation(second))) {
        return false;
      }
      index += 2;
      continue;
    }
    if (first >= 0xf0 && first <= 0xf4) {
      if (index + 2 >= value.size()) return false;
      const auto second = bytes[index];
      if ((first == 0xf0 ? second < 0x90 || second > 0xbf
                         : first == 0xf4 ? second < 0x80 || second > 0x8f
                                         : !continuation(second)) ||
          !continuation(bytes[index + 1]) || !continuation(bytes[index + 2])) {
        return false;
      }
      index += 3;
      continue;
    }
    return false;
  }
  return true;
}

Result<OcrResult> assemble_ocr_result(
    std::uint32_t image_width, std::uint32_t image_height,
    std::string model_bundle_id, Timing timing,
    std::uint32_t detected_candidates, std::vector<Quad> boxes,
    std::vector<DecodedText> decoded, float score_threshold,
    bool include_diagnostics) noexcept {
  try {
    if (image_width == 0 || image_height == 0 || model_bundle_id.empty() ||
        !std::isfinite(score_threshold) || score_threshold < 0 ||
        score_threshold > 1 || boxes.size() != decoded.size()) {
      return assembly_failure("Result assembly contract is invalid");
    }

    OcrResult result;
    result.image_width = image_width;
    result.image_height = image_height;
    result.model_bundle_id = std::move(model_bundle_id);
    result.timing = timing;
    if (include_diagnostics) {
      result.diagnostics.emplace();
      result.diagnostics->detected_candidates = detected_candidates;
      result.diagnostics->accepted_boxes = static_cast<std::uint32_t>(boxes.size());
    }
    result.lines.reserve(boxes.size());
    for (std::size_t index = 0; index < boxes.size(); ++index) {
      if (!valid_quad(boxes[index], image_width, image_height) ||
          !valid_utf8(decoded[index].text) ||
          !std::isfinite(decoded[index].confidence) ||
          decoded[index].confidence < 0 || decoded[index].confidence > 1) {
        return assembly_failure("Decoded OCR line violates the public result contract");
      }
      OcrLine line{std::move(decoded[index].text), decoded[index].confidence,
                   std::move(boxes[index])};
      if (line.text.empty()) {
        if (result.diagnostics) {
          result.diagnostics->rejected_lines.push_back(
              RejectedLine{std::move(line), RejectionReason::empty_decode});
        }
      } else if (line.confidence < score_threshold) {
        if (result.diagnostics) {
          result.diagnostics->rejected_lines.push_back(
              RejectedLine{std::move(line),
                           RejectionReason::below_score_threshold});
        }
      } else {
        result.lines.push_back(std::move(line));
      }
    }
    return Result<OcrResult>::success(std::move(result));
  } catch (const std::exception& exception) {
    return Result<OcrResult>::failure(
        Error{ErrorCode::internal_error, "Unexpected result assembly failure",
              exception.what()});
  } catch (...) {
    return Result<OcrResult>::failure(
        Error{ErrorCode::internal_error, "Unknown result assembly failure", {}});
  }
}

}  // namespace light_ocr::internal
