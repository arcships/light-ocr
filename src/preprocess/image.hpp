#pragma once

#include <opencv2/core.hpp>

#include "light_ocr/error.hpp"
#include "light_ocr/types.hpp"

namespace light_ocr::internal {

struct ValidatedImageLayout {
  std::size_t channels = 0;
  int cv_type = 0;
  std::size_t required_bytes = 0;
  std::uint64_t converted_bytes = 0;
};

struct ValidatedImage {
  cv::Mat bgr;
  std::size_t required_bytes = 0;
};

Result<ValidatedImageLayout> validate_image_layout(const ImageView& image,
                                                   const ResourceLimits& limits);
Result<ValidatedImage> validate_and_convert_image(const ImageView& image,
                                                  const ResourceLimits& limits);

}  // namespace light_ocr::internal
