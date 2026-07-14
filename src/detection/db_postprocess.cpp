#include "detection/db_postprocess.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <utility>
#include <vector>

#include <clipper.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "geometry/geometry.hpp"
#include "util/checked_math.hpp"
#include "util/sha256.hpp"

namespace light_ocr::internal {
namespace {

double polygon_area(const Quad& quad) {
  double sum = 0;
  for (std::size_t i = 0; i < quad.points.size(); ++i) {
    const auto& left = quad.points[i];
    const auto& right = quad.points[(i + 1) % quad.points.size()];
    sum += static_cast<double>(left.x) * right.y - static_cast<double>(right.x) * left.y;
  }
  return std::abs(sum) / 2.0;
}

double polygon_perimeter(const Quad& quad) {
  double result = 0;
  for (std::size_t i = 0; i < quad.points.size(); ++i) {
    const auto& left = quad.points[i];
    const auto& right = quad.points[(i + 1) % quad.points.size()];
    result += std::hypot(static_cast<double>(right.x) - static_cast<double>(left.x),
                         static_cast<double>(right.y) - static_cast<double>(left.y));
  }
  return result;
}

std::vector<cv::Point2f> unclip(const Quad& quad, float ratio) {
  const auto area = polygon_area(quad);
  const auto perimeter = polygon_perimeter(quad);
  if (!std::isfinite(area) || !std::isfinite(perimeter) || area <= 0 || perimeter <= 0) return {};
  const auto distance = area * ratio / perimeter;
  // pyclipper's AddPath contract used by PaddleOCR converts the min-area
  // rectangle to Clipper's integer coordinate grid before offsetting.
  ClipperLib::Path path;
  path.reserve(quad.points.size());
  for (const auto& point : quad.points) {
    path.emplace_back(static_cast<ClipperLib::cInt>(point.x),
                      static_cast<ClipperLib::cInt>(point.y));
  }
  ClipperLib::ClipperOffset offset(2.0, 0.25);
  offset.AddPath(path, ClipperLib::jtRound, ClipperLib::etClosedPolygon);
  ClipperLib::Paths expanded;
  offset.Execute(expanded, distance);
  if (expanded.size() != 1 || expanded.front().size() < 3) return {};
  std::vector<cv::Point2f> result;
  result.reserve(expanded.front().size());
  for (const auto& point : expanded.front()) {
    result.emplace_back(static_cast<float>(point.X), static_cast<float>(point.Y));
  }
  return result;
}

float box_score_fast(const cv::Mat& probability, const Quad& quad) {
  auto minimum_x = std::numeric_limits<float>::max();
  auto maximum_x = std::numeric_limits<float>::lowest();
  auto minimum_y = std::numeric_limits<float>::max();
  auto maximum_y = std::numeric_limits<float>::lowest();
  for (const auto& point : quad.points) {
    minimum_x = std::min(minimum_x, point.x);
    maximum_x = std::max(maximum_x, point.x);
    minimum_y = std::min(minimum_y, point.y);
    maximum_y = std::max(maximum_y, point.y);
  }
  const auto xmin = std::clamp(static_cast<int>(std::floor(minimum_x)), 0, probability.cols - 1);
  const auto xmax = std::clamp(static_cast<int>(std::ceil(maximum_x)), 0, probability.cols - 1);
  const auto ymin = std::clamp(static_cast<int>(std::floor(minimum_y)), 0, probability.rows - 1);
  const auto ymax = std::clamp(static_cast<int>(std::ceil(maximum_y)), 0, probability.rows - 1);
  cv::Mat mask(ymax - ymin + 1, xmax - xmin + 1, CV_8UC1, cv::Scalar(0));
  std::vector<cv::Point> points;
  points.reserve(quad.points.size());
  for (const auto& point : quad.points) {
    points.emplace_back(static_cast<int>(point.x - xmin), static_cast<int>(point.y - ymin));
  }
  cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{points}, cv::Scalar(1));
  const auto roi = probability(cv::Range(ymin, ymax + 1), cv::Range(xmin, xmax + 1));
  return static_cast<float>(cv::mean(roi, mask)[0]);
}

double round_half_even(double value) {
  const double floor_value = std::floor(value);
  const double fraction = value - floor_value;
  if (fraction < 0.5) return floor_value;
  if (fraction > 0.5) return floor_value + 1.0;
  return std::fmod(floor_value, 2.0) == 0.0 ? floor_value : floor_value + 1.0;
}

Point restore_point(const Point& point, double scale_x, double scale_y,
                    std::uint32_t width, std::uint32_t height) {
  return Point{static_cast<float>(std::clamp(round_half_even(point.x * scale_x), 0.0,
                                             static_cast<double>(width))),
               static_cast<float>(std::clamp(round_half_even(point.y * scale_y), 0.0,
                                             static_cast<double>(height)))};
}

}  // namespace

Result<DetectionBoxes> db_postprocess(const float* probabilities, std::size_t element_count,
                                      const std::vector<std::int64_t>& shape,
                                      std::uint32_t original_width,
                                      std::uint32_t original_height,
                                      const DetectionConfig& config,
                                      const ResourceLimits& limits,
                                      bool include_trace) {
  try {
    if (probabilities == nullptr || (shape.size() != 3 && shape.size() != 4) ||
        original_width == 0 || original_height == 0) {
      return Result<DetectionBoxes>::failure(
          Error{ErrorCode::postprocess_failed, "Detection output contract is invalid", {}});
    }
    const auto height_index = shape.size() - 2;
    const auto width_index = shape.size() - 1;
    if (shape[0] != 1 || shape[height_index] <= 0 || shape[width_index] <= 0 ||
        (shape.size() == 4 && shape[1] != 1)) {
      return Result<DetectionBoxes>::failure(
          Error{ErrorCode::postprocess_failed, "Detection output shape is unsupported", {}});
    }
    const auto height = static_cast<std::uint64_t>(shape[height_index]);
    const auto width = static_cast<std::uint64_t>(shape[width_index]);
    std::uint64_t expected = 0;
    if (!checked_mul(height, width, &expected) || expected != element_count ||
        height > std::numeric_limits<int>::max() || width > std::numeric_limits<int>::max()) {
      return Result<DetectionBoxes>::failure(
          Error{ErrorCode::postprocess_failed, "Detection output size does not match shape", {}});
    }
    for (std::size_t i = 0; i < element_count; ++i) {
      if (!std::isfinite(probabilities[i])) {
        return Result<DetectionBoxes>::failure(
            Error{ErrorCode::postprocess_failed, "Detection output contains a non-finite value", {}});
      }
    }

    cv::Mat probability(static_cast<int>(height), static_cast<int>(width), CV_32FC1,
                        const_cast<float*>(probabilities));
    cv::Mat bitmap;
    cv::threshold(probability, bitmap, config.threshold, 255, cv::THRESH_BINARY);
    bitmap.convertTo(bitmap, CV_8UC1);
    if (config.use_dilation) {
      const auto kernel = cv::Mat::ones(2, 2, CV_8UC1);
      cv::dilate(bitmap, bitmap, kernel);
    }
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bitmap, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    const auto candidates = std::min<std::size_t>(
        contours.size(), std::min<std::uint32_t>(config.max_candidates,
                                                limits.max_detection_candidates));
    DetectionBoxes output;
    output.contour_candidates = static_cast<std::uint32_t>(candidates);
    output.boxes.reserve(candidates);
    if (include_trace) {
      std::vector<std::uint8_t> packed_bitmap;
      const auto row_bytes = static_cast<std::size_t>(bitmap.cols);
      packed_bitmap.reserve(row_bytes * static_cast<std::size_t>(bitmap.rows));
      for (int row = 0; row < bitmap.rows; ++row) {
        const auto* begin = bitmap.ptr<std::uint8_t>(row);
        packed_bitmap.insert(packed_bitmap.end(), begin, begin + row_bytes);
      }
      output.threshold_bitmap_sha256 =
          sha256_hex(packed_bitmap.data(), packed_bitmap.size());
      output.traces.reserve(candidates);
    }
    const double scale_x = static_cast<double>(original_width) / width;
    const double scale_y = static_cast<double>(original_height) / height;
    for (std::size_t index = 0; index < candidates; ++index) {
      DetectionCandidateTrace trace;
      trace.candidate_index = static_cast<std::uint32_t>(index);
      const auto finish_trace = [&](const char* decision) {
        if (include_trace) {
          trace.decision = decision;
          output.traces.push_back(std::move(trace));
        }
      };
      const auto rect = cv::minAreaRect(contours[index]);
      cv::Point2f raw_points[4];
      rect.points(raw_points);
      auto quad = order_quad(raw_points);
      if (include_trace) trace.initial_quad = quad;
      if (std::min(rect.size.width, rect.size.height) < config.minimum_box_side) {
        finish_trace("initial_side_too_small");
        continue;
      }
      const auto score = box_score_fast(probability, quad);
      if (include_trace) trace.score = score;
      if (score < config.box_threshold) {
        finish_trace("below_box_threshold");
        continue;
      }
      const auto expanded_points = unclip(quad, config.unclip_ratio);
      if (include_trace) {
        trace.expanded_polygon.reserve(expanded_points.size());
        for (const auto& point : expanded_points) {
          trace.expanded_polygon.push_back(Point{point.x, point.y});
        }
      }
      if (expanded_points.size() < 3) {
        finish_trace("unclip_failed");
        continue;
      }
      const auto expanded_rect = cv::minAreaRect(expanded_points);
      if (std::min(expanded_rect.size.width, expanded_rect.size.height) <
          config.minimum_box_side + 2.0f) {
        finish_trace("expanded_side_too_small");
        continue;
      }
      cv::Point2f expanded_raw[4];
      expanded_rect.points(expanded_raw);
      const auto expanded_quad = order_quad(expanded_raw);
      if (include_trace) trace.expanded_quad = expanded_quad;
      Quad restored;
      for (std::size_t point = 0; point < restored.points.size(); ++point) {
        restored.points[point] = restore_point(expanded_quad.points[point], scale_x, scale_y,
                                               original_width, original_height);
      }
      if (include_trace) trace.restored_quad = restored;
      const auto box_width = std::hypot(restored.points[1].x - restored.points[0].x,
                                        restored.points[1].y - restored.points[0].y);
      const auto box_height = std::hypot(restored.points[3].x - restored.points[0].x,
                                         restored.points[3].y - restored.points[0].y);
      if (box_width <= config.minimum_box_side || box_height <= config.minimum_box_side) {
        finish_trace("restored_side_too_small");
        continue;
      }
      output.boxes.push_back(restored);
      finish_trace("accepted");
    }
    return Result<DetectionBoxes>::success(std::move(output));
  } catch (const cv::Exception& exception) {
    return Result<DetectionBoxes>::failure(
        Error{ErrorCode::postprocess_failed, "OpenCV failed during DB postprocessing", exception.err});
  } catch (const std::exception& exception) {
    return Result<DetectionBoxes>::failure(
        Error{ErrorCode::postprocess_failed, "Unexpected DB postprocessing failure", exception.what()});
  } catch (...) {
    return Result<DetectionBoxes>::failure(
        Error{ErrorCode::internal_error, "Unknown DB postprocessing failure", {}});
  }
}

}  // namespace light_ocr::internal
