#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/core/utils/logger.hpp>

#include "geometry/geometry.hpp"
#include "light_ocr/types.hpp"
#include "model/bundle_data.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_SILENT);
  if (size < sizeof(float) * 8) return 0;
  light_ocr::Quad quad;
  for (std::size_t index = 0; index < 4; ++index) {
    std::memcpy(&quad.points[index].x, data + index * sizeof(float) * 2, sizeof(float));
    std::memcpy(&quad.points[index].y, data + index * sizeof(float) * 2 + sizeof(float),
                sizeof(float));
  }
  cv::Mat image(64, 64, CV_8UC3, cv::Scalar(1, 2, 3));
  light_ocr::internal::GeometryConfig config{10, 1.5f};
  light_ocr::ResourceLimits limits;
  limits.max_temporary_bytes = size > 32 ? data[32] * 1024ull + 1 : 4096;
  (void)light_ocr::internal::sort_reading_order({quad}, config);
  (void)light_ocr::internal::crop_text_regions(image, {quad}, config, limits);
  return 0;
}
