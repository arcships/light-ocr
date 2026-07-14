#include "preprocess/image.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>

#include <opencv2/imgproc.hpp>

#include "util/checked_math.hpp"

namespace light_ocr::internal {
namespace {

template <class T>
Result<T> image_error(ErrorCode code, const char* message) {
  return Result<T>::failure(Error{code, message, {}});
}

}  // namespace

Result<ValidatedImageLayout> validate_image_layout(const ImageView& image,
                                                   const ResourceLimits& limits) {
  if (image.data == nullptr || image.width == 0 || image.height == 0) {
    return image_error<ValidatedImageLayout>(
        ErrorCode::invalid_image, "Image data and dimensions must be non-empty");
  }

  std::size_t channels = 0;
  int cv_type = 0;
  switch (image.pixel_format) {
    case PixelFormat::gray8:
      channels = 1;
      cv_type = CV_8UC1;
      break;
    case PixelFormat::rgb8:
    case PixelFormat::bgr8:
      channels = 3;
      cv_type = CV_8UC3;
      break;
    case PixelFormat::rgba8:
      channels = 4;
      cv_type = CV_8UC4;
      break;
    default:
      return image_error<ValidatedImageLayout>(
          ErrorCode::unsupported_pixel_format,
          "Pixel format is not supported by this core version");
  }

  std::size_t row_bytes = 0;
  if (!checked_mul<std::size_t>(image.width, channels, &row_bytes) || image.stride < row_bytes) {
    return image_error<ValidatedImageLayout>(
        ErrorCode::invalid_image, "Image stride is smaller than its row width");
  }
  std::size_t required_bytes = 0;
  if (!checked_image_bytes(image.height, image.stride, row_bytes, &required_bytes) ||
      required_bytes > image.size) {
    return image_error<ValidatedImageLayout>(
        ErrorCode::invalid_image, "Image buffer is truncated or overflows its size");
  }

  std::uint64_t pixels = 0;
  if (!checked_mul<std::uint64_t>(image.width, image.height, &pixels)) {
    return image_error<ValidatedImageLayout>(
        ErrorCode::resource_limit_exceeded, "Image pixel count overflows");
  }
  if (image.width > limits.max_width || image.height > limits.max_height ||
      pixels > limits.max_pixels) {
    return image_error<ValidatedImageLayout>(
        ErrorCode::resource_limit_exceeded, "Image exceeds engine resource limits");
  }

  std::uint64_t converted_bytes = 0;
  if (!checked_mul<std::uint64_t>(pixels, 3, &converted_bytes) ||
      converted_bytes > limits.max_temporary_bytes) {
    return image_error<ValidatedImageLayout>(
        ErrorCode::resource_limit_exceeded, "Converted image exceeds temporary memory limit");
  }
  return Result<ValidatedImageLayout>::success(
      ValidatedImageLayout{channels, cv_type, required_bytes, converted_bytes});
}

Result<ValidatedImage> validate_and_convert_image(const ImageView& image,
                                                  const ResourceLimits& limits) {
  try {
    auto layout_result = validate_image_layout(image, limits);
    if (!layout_result) {
      return Result<ValidatedImage>::failure(layout_result.error());
    }
    const auto& layout = layout_result.value();
    const cv::Mat source(static_cast<int>(image.height), static_cast<int>(image.width),
                         layout.cv_type,
                         const_cast<std::uint8_t*>(image.data), image.stride);
    cv::Mat bgr;
    switch (image.pixel_format) {
      case PixelFormat::gray8:
        cv::cvtColor(source, bgr, cv::COLOR_GRAY2BGR);
        break;
      case PixelFormat::rgb8:
        cv::cvtColor(source, bgr, cv::COLOR_RGB2BGR);
        break;
      case PixelFormat::bgr8:
        bgr = source.clone();
        break;
      case PixelFormat::rgba8:
        cv::cvtColor(source, bgr, cv::COLOR_RGBA2BGR);
        break;
      default:
        return image_error<ValidatedImage>(ErrorCode::unsupported_pixel_format,
                                           "Pixel format is not supported by this core version");
    }
    if (bgr.empty() || bgr.type() != CV_8UC3) {
      return image_error<ValidatedImage>(ErrorCode::internal_error,
                                         "Image conversion produced an invalid matrix");
    }
    return Result<ValidatedImage>::success(
        ValidatedImage{std::move(bgr), layout.required_bytes});
  } catch (const cv::Exception& exception) {
    return Result<ValidatedImage>::failure(
        Error{ErrorCode::invalid_image, "OpenCV failed to convert the image", exception.err});
  } catch (const std::exception& exception) {
    return Result<ValidatedImage>::failure(
        Error{ErrorCode::internal_error, "Unexpected image validation failure", exception.what()});
  } catch (...) {
    return Result<ValidatedImage>::failure(
        Error{ErrorCode::internal_error, "Unknown image validation failure", {}});
  }
}

}  // namespace light_ocr::internal
