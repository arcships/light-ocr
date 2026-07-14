#pragma once

#include <vector>

#include <opencv2/core.hpp>

#include "light_ocr/error.hpp"
#include "light_ocr/types.hpp"
#include "model/bundle_data.hpp"

namespace light_ocr::internal {

Quad order_quad(const cv::Point2f points[4]);
std::vector<Quad> sort_reading_order(std::vector<Quad> boxes,
                                     const GeometryConfig& config);
Result<std::vector<cv::Mat>> crop_text_regions(const cv::Mat& bgr,
                                               const std::vector<Quad>& boxes,
                                               const GeometryConfig& config,
                                               const ResourceLimits& limits);

}  // namespace light_ocr::internal
