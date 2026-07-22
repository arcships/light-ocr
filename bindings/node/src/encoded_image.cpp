#include "encoded_image.hpp"
#include "exif.hpp"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <string>

namespace {

enum class DecodeAllocationFailure {
  none,
  budget_exceeded,
  size_overflow,
  system_allocation_failed,
  accounting_error,
};

struct DecodeBudget {
  std::size_t limit = 0;
  std::size_t current = 0;
  std::size_t peak = 0;
  DecodeAllocationFailure failure = DecodeAllocationFailure::none;
};

struct alignas(std::max_align_t) AllocationHeader {
  std::size_t size = 0;
};

thread_local DecodeBudget* active_decode_budget = nullptr;

class DecodeBudgetScope {
 public:
  explicit DecodeBudgetScope(DecodeBudget* budget)
      : previous_(active_decode_budget) {
    active_decode_budget = budget;
  }

  ~DecodeBudgetScope() { active_decode_budget = previous_; }

  DecodeBudgetScope(const DecodeBudgetScope&) = delete;
  DecodeBudgetScope& operator=(const DecodeBudgetScope&) = delete;

 private:
  DecodeBudget* previous_ = nullptr;
};

bool allocation_size(std::size_t requested, std::size_t* total) {
  if (requested > std::numeric_limits<std::size_t>::max() -
                      sizeof(AllocationHeader)) {
    return false;
  }
  *total = requested + sizeof(AllocationHeader);
  return true;
}

void mark_allocation_failure(DecodeAllocationFailure failure) {
  if (active_decode_budget != nullptr &&
      active_decode_budget->failure == DecodeAllocationFailure::none) {
    active_decode_budget->failure = failure;
  }
}

void* stbi_budget_malloc(std::size_t requested) {
  std::size_t total = 0;
  if (!allocation_size(requested, &total)) {
    mark_allocation_failure(DecodeAllocationFailure::size_overflow);
    return nullptr;
  }
  if (active_decode_budget != nullptr) {
    if (active_decode_budget->current > active_decode_budget->limit ||
        total > active_decode_budget->limit - active_decode_budget->current) {
      mark_allocation_failure(DecodeAllocationFailure::budget_exceeded);
      return nullptr;
    }
  }
  auto* header = static_cast<AllocationHeader*>(std::malloc(total));
  if (header == nullptr) {
    mark_allocation_failure(DecodeAllocationFailure::system_allocation_failed);
    return nullptr;
  }
  header->size = total;
  if (active_decode_budget != nullptr) {
    active_decode_budget->current += total;
    active_decode_budget->peak =
        std::max(active_decode_budget->peak, active_decode_budget->current);
  }
  return header + 1;
}

void stbi_budget_free(void* pointer) {
  if (pointer == nullptr) return;
  auto* header = static_cast<AllocationHeader*>(pointer) - 1;
  if (active_decode_budget != nullptr) {
    if (header->size > active_decode_budget->current) {
      mark_allocation_failure(DecodeAllocationFailure::accounting_error);
      active_decode_budget->current = 0;
    } else {
      active_decode_budget->current -= header->size;
    }
  }
  std::free(header);
}

void* stbi_budget_realloc(void* pointer, std::size_t old_size,
                          std::size_t requested) {
  (void)old_size;
  if (pointer == nullptr) return stbi_budget_malloc(requested);
  if (requested == 0) {
    stbi_budget_free(pointer);
    return nullptr;
  }
  auto* old_header = static_cast<AllocationHeader*>(pointer) - 1;
  const std::size_t old_total = old_header->size;
  std::size_t new_total = 0;
  if (!allocation_size(requested, &new_total)) {
    mark_allocation_failure(DecodeAllocationFailure::size_overflow);
    return nullptr;
  }
  if (active_decode_budget != nullptr) {
    if (old_total > active_decode_budget->current) {
      mark_allocation_failure(DecodeAllocationFailure::accounting_error);
      return nullptr;
    }
    if (new_total > old_total &&
        (active_decode_budget->current > active_decode_budget->limit ||
         new_total - old_total > active_decode_budget->limit -
                                     active_decode_budget->current)) {
      mark_allocation_failure(DecodeAllocationFailure::budget_exceeded);
      return nullptr;
    }
  }
  auto* new_header =
      static_cast<AllocationHeader*>(std::realloc(old_header, new_total));
  if (new_header == nullptr) {
    mark_allocation_failure(DecodeAllocationFailure::system_allocation_failed);
    return nullptr;
  }
  new_header->size = new_total;
  if (active_decode_budget != nullptr) {
    active_decode_budget->current =
        active_decode_budget->current - old_total + new_total;
    active_decode_budget->peak =
        std::max(active_decode_budget->peak, active_decode_budget->current);
  }
  return new_header + 1;
}

}  // namespace

#define STB_IMAGE_STATIC
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#define STBI_MALLOC(size) stbi_budget_malloc(size)
#define STBI_REALLOC_SIZED(pointer, old_size, new_size) \
  stbi_budget_realloc(pointer, old_size, new_size)
#define STBI_FREE(pointer) stbi_budget_free(pointer)
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

Result<DecodedImage> allocation_failure(const DecodeBudget& budget) {
  switch (budget.failure) {
    case DecodeAllocationFailure::budget_exceeded:
      return failure(ErrorCode::resource_limit_exceeded,
                     "Encoded image decoder exceeded its memory budget");
    case DecodeAllocationFailure::size_overflow:
      return failure(ErrorCode::resource_limit_exceeded,
                     "Encoded image decoder allocation size overflowed");
    case DecodeAllocationFailure::system_allocation_failed:
      return failure(ErrorCode::resource_limit_exceeded,
                     "Encoded image decoder ran out of memory");
    case DecodeAllocationFailure::accounting_error:
      return failure(ErrorCode::internal_error,
                     "Encoded image decoder memory accounting failed");
    case DecodeAllocationFailure::none:
      break;
  }
  return failure(ErrorCode::resource_limit_exceeded,
                 "Encoded image decoder exceeded its memory budget");
}

}  // namespace

Result<DecodedImage> decode_encoded_image(
    const std::vector<std::uint8_t>& encoded,
    const ResourceLimits& limits,
    bool apply_exif) noexcept {
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
    const auto budget_limit = static_cast<std::size_t>(std::min<std::uint64_t>(
        limits.max_temporary_bytes,
        std::numeric_limits<std::size_t>::max()));
    DecodeBudget budget{budget_limit};
    DecodeBudgetScope budget_scope(&budget);
    if (stbi_info_from_memory(data, size, &width, &height, &source_channels) == 0 ||
        width <= 0 || height <= 0) {
      if (budget.failure != DecodeAllocationFailure::none) {
        return allocation_failure(budget);
      }
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
      if (budget.failure != DecodeAllocationFailure::none) {
        return allocation_failure(budget);
      }
      return failure(ErrorCode::invalid_image, "Failed to decode JPEG or PNG image",
                     decoder_detail());
    }
    if (static_cast<std::uint64_t>(width) != decoded_width ||
        static_cast<std::uint64_t>(height) != decoded_height) {
      return failure(ErrorCode::invalid_image,
                     "Encoded image dimensions changed during decoding");
    }

    DecodedImage result;
    const auto decoded_size = static_cast<std::size_t>(decoded_bytes);
    if (budget.current > budget.limit ||
        decoded_size > budget.limit - budget.current) {
      budget.failure = DecodeAllocationFailure::budget_exceeded;
      return allocation_failure(budget);
    }
    result.bytes.assign(pixels_data.get(),
                        pixels_data.get() + decoded_size);
    pixels_data.reset();
    if (budget.current != 0) {
      return failure(ErrorCode::internal_error,
                     "Encoded image decoder retained temporary allocations");
    }
    result.width = static_cast<std::uint32_t>(decoded_width);
    result.height = static_cast<std::uint32_t>(decoded_height);
    result.stride = static_cast<std::size_t>(decoded_width * kOutputChannels);
    result.pixel_format = PixelFormat::rgb8;

    // EXIF orientation correction (D106, D-N1-5): parse the orientation tag
    // from the original JPEG bytes and apply the pixel transform before
    // returning the decoded image to the recognition pipeline.
    if (apply_exif) {
      const std::uint16_t orientation = exif::parse_orientation(encoded);
      if (orientation != 1) {
        result = exif::apply_orientation(std::move(result), orientation);
      }
    }

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

DecodedImage crop_decoded_image(DecodedImage image, const Rect& region) noexcept {
  const std::uint32_t channels = 3;  // rgb8
  DecodedImage out;
  out.width = region.width;
  out.height = region.height;
  out.stride = static_cast<std::size_t>(region.width) * channels;
  out.pixel_format = PixelFormat::rgb8;
  out.bytes.resize(static_cast<std::size_t>(region.width) * region.height * channels);

  for (std::uint32_t row = 0; row < region.height; ++row) {
    const std::uint8_t* src =
        image.bytes.data() +
        static_cast<std::size_t>(region.y + row) * image.stride +
        static_cast<std::size_t>(region.x) * channels;
    std::uint8_t* dst = out.bytes.data() +
        static_cast<std::size_t>(row) * out.stride;
    std::memcpy(dst, src, static_cast<std::size_t>(region.width) * channels);
  }
  return out;
}

}  // namespace light_ocr::node
