#include "encoded_image.hpp"

#include <climits>
#include <cstdlib>
#include <exception>
#include <limits>
#include <memory>
#include <string>

#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace light_ocr::node {
namespace {

Result<DecodedImage> failure(ErrorCode code, std::string message,
                             std::string detail = {}) {
  return Result<DecodedImage>::failure(
      Error{code, std::move(message), std::move(detail)});
}

std::string decoder_detail() {
  const char* reason = stbi_failure_reason();
  return reason == nullptr ? std::string{} : std::string(reason);
}

}  // namespace

Result<DecodedImage> decode_encoded_image(
    const std::vector<std::uint8_t>& encoded,
    const ResourceLimits& limits) noexcept {
  try {
    if (encoded.empty()) {
      return failure(ErrorCode::invalid_image, "Encoded image is empty");
    }
    if (encoded.size() > static_cast<std::size_t>(INT_MAX)) {
      return failure(ErrorCode::resource_limit_exceeded,
                     "Encoded image exceeds decoder limits");
    }

    int width = 0;
    int height = 0;
    int source_channels = 0;
    const auto* data = encoded.data();
    const int size = static_cast<int>(encoded.size());
    if (stbi_info_from_memory(data, size, &width, &height, &source_channels) == 0 ||
        width <= 0 || height <= 0) {
      return failure(ErrorCode::invalid_image,
                     "Input is not a supported JPEG or PNG image", decoder_detail());
    }

    const auto decoded_width = static_cast<std::uint64_t>(width);
    const auto decoded_height = static_cast<std::uint64_t>(height);
    if (decoded_width > limits.max_width || decoded_height > limits.max_height ||
        decoded_width > std::numeric_limits<std::uint64_t>::max() / decoded_height ||
        decoded_width * decoded_height > limits.max_pixels) {
      return failure(ErrorCode::resource_limit_exceeded,
                     "Decoded image dimensions exceed engine limits");
    }
    constexpr std::uint64_t kOutputChannels = 3;
    const std::uint64_t pixels = decoded_width * decoded_height;
    if (pixels > std::numeric_limits<std::uint64_t>::max() / kOutputChannels) {
      return failure(ErrorCode::resource_limit_exceeded,
                     "Decoded image byte size overflows");
    }
    const std::uint64_t decoded_bytes = pixels * kOutputChannels;
    // The decoded RGB pixels coexist first with stb's output and later with
    // Core's BGR conversion, so reserve for two full decoded buffers.
    if (decoded_bytes > limits.max_temporary_bytes / 2 ||
        decoded_bytes > std::numeric_limits<std::size_t>::max()) {
      return failure(ErrorCode::resource_limit_exceeded,
                     "Decoded image exceeds the temporary memory budget");
    }

    using StbiPixels = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>;
    StbiPixels pixels_data(
        stbi_load_from_memory(data, size, &width, &height, &source_channels,
                              static_cast<int>(kOutputChannels)),
        stbi_image_free);
    if (!pixels_data) {
      return failure(ErrorCode::invalid_image, "Failed to decode JPEG or PNG image",
                     decoder_detail());
    }
    if (static_cast<std::uint64_t>(width) != decoded_width ||
        static_cast<std::uint64_t>(height) != decoded_height) {
      return failure(ErrorCode::invalid_image,
                     "Encoded image dimensions changed during decoding");
    }

    DecodedImage result;
    result.bytes.assign(pixels_data.get(),
                        pixels_data.get() + static_cast<std::size_t>(decoded_bytes));
    result.width = static_cast<std::uint32_t>(decoded_width);
    result.height = static_cast<std::uint32_t>(decoded_height);
    result.stride = static_cast<std::size_t>(decoded_width * kOutputChannels);
    result.pixel_format = PixelFormat::rgb8;
    return Result<DecodedImage>::success(std::move(result));
  } catch (const std::bad_alloc&) {
    return failure(ErrorCode::resource_limit_exceeded,
                   "Encoded image decoding ran out of memory");
  } catch (const std::exception& exception) {
    return failure(ErrorCode::internal_error,
                   "Unexpected encoded image decoding failure", exception.what());
  } catch (...) {
    return failure(ErrorCode::internal_error,
                   "Unknown encoded image decoding failure");
  }
}

}  // namespace light_ocr::node
