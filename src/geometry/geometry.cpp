#include "geometry/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <utility>
#include <vector>

#include <opencv2/imgproc.hpp>

#include "util/checked_math.hpp"

namespace light_ocr::internal {
namespace {

bool valid_clockwise_convex_quad(const Quad& quad) {
  for (std::size_t index = 0; index < quad.points.size(); ++index) {
    const auto& point = quad.points[index];
    const auto& next = quad.points[(index + 1) % quad.points.size()];
    const auto& following = quad.points[(index + 2) % quad.points.size()];
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) return false;
    const auto cross =
        (static_cast<double>(next.x) - point.x) *
            (static_cast<double>(following.y) - next.y) -
        (static_cast<double>(next.y) - point.y) *
            (static_cast<double>(following.x) - next.x);
    if (!std::isfinite(cross) || cross <= 0) return false;
  }
  return true;
}

double distance(const Point& left, const Point& right) {
  return std::hypot(static_cast<double>(left.x) - static_cast<double>(right.x),
                    static_cast<double>(left.y) - static_cast<double>(right.y));
}

}  // namespace

Quad order_quad(const cv::Point2f points[4]) {
  std::array<cv::Point2f, 4> sorted = {points[0], points[1], points[2], points[3]};
  std::stable_sort(sorted.begin(), sorted.end(), [](const auto& left, const auto& right) {
    if (left.x == right.x) return left.y < right.y;
    return left.x < right.x;
  });
  const auto top_left_index = sorted[1].y > sorted[0].y ? 0u : 1u;
  const auto bottom_left_index = top_left_index == 0 ? 1u : 0u;
  const auto top_right_index = sorted[3].y > sorted[2].y ? 2u : 3u;
  const auto bottom_right_index = top_right_index == 2 ? 3u : 2u;
  Quad result;
  const std::array<cv::Point2f, 4> ordered = {sorted[top_left_index], sorted[top_right_index],
                                              sorted[bottom_right_index], sorted[bottom_left_index]};
  for (std::size_t i = 0; i < ordered.size(); ++i) {
    result.points[i] = Point{ordered[i].x, ordered[i].y};
  }
  return result;
}

std::vector<Quad> sort_reading_order(std::vector<Quad> boxes,
                                     const GeometryConfig& config) {
  std::stable_sort(boxes.begin(), boxes.end(), [](const auto& left, const auto& right) {
    if (left.points[0].y == right.points[0].y) return left.points[0].x < right.points[0].x;
    return left.points[0].y < right.points[0].y;
  });
  for (std::size_t i = 0; i + 1 < boxes.size(); ++i) {
    auto j = i;
    while (true) {
      auto& current = boxes[j];
      auto& next = boxes[j + 1];
      if (std::abs(next.points[0].y - current.points[0].y) < config.row_band_pixels &&
          next.points[0].x < current.points[0].x) {
        std::swap(current, next);
        if (j == 0) break;
        --j;
      } else {
        break;
      }
    }
  }
  return boxes;
}

Result<std::vector<cv::Mat>> crop_text_regions(const cv::Mat& bgr,
                                               const std::vector<Quad>& boxes,
                                               const GeometryConfig& config,
                                               const ResourceLimits& limits) {
  try {
    if (bgr.empty() || bgr.type() != CV_8UC3) {
      return Result<std::vector<cv::Mat>>::failure(
          Error{ErrorCode::invalid_image, "Crop source must be a non-empty BGR8 image", {}});
    }
    std::vector<cv::Mat> crops;
    crops.reserve(boxes.size());
    std::uint64_t aggregate_bytes = 0;
    for (const auto& box : boxes) {
      if (!valid_clockwise_convex_quad(box)) {
        return Result<std::vector<cv::Mat>>::failure(
            Error{ErrorCode::postprocess_failed,
                  "Detected quadrilateral is non-finite, degenerate, concave, or unordered", {}});
      }
      const auto width_value = std::max(distance(box.points[0], box.points[1]),
                                        distance(box.points[2], box.points[3]));
      const auto height_value = std::max(distance(box.points[0], box.points[3]),
                                         distance(box.points[1], box.points[2]));
      if (!std::isfinite(width_value) || !std::isfinite(height_value) ||
          width_value > std::numeric_limits<int>::max() ||
          height_value > std::numeric_limits<int>::max()) {
        return Result<std::vector<cv::Mat>>::failure(
            Error{ErrorCode::resource_limit_exceeded,
                  "Detected quadrilateral produces an unsupported crop size", {}});
      }
      if (width_value < 1 || height_value < 1) {
        return Result<std::vector<cv::Mat>>::failure(
            Error{ErrorCode::postprocess_failed, "Detected quadrilateral produces an empty crop", {}});
      }
      const auto width = static_cast<std::uint32_t>(width_value);
      const auto height = static_cast<std::uint32_t>(height_value);
      std::uint64_t crop_bytes = 0;
      if (!checked_mul<std::uint64_t>(width, height, &crop_bytes) ||
          !checked_mul<std::uint64_t>(crop_bytes, 3, &crop_bytes) ||
          !checked_add<std::uint64_t>(aggregate_bytes, crop_bytes, &aggregate_bytes) ||
          aggregate_bytes > limits.max_temporary_bytes) {
        return Result<std::vector<cv::Mat>>::failure(
            Error{ErrorCode::resource_limit_exceeded, "Text crops exceed temporary memory limit", {}});
      }
      std::array<cv::Point2f, 4> source{};
      for (std::size_t i = 0; i < source.size(); ++i) {
        source[i] = cv::Point2f(box.points[i].x, box.points[i].y);
      }
      const std::array<cv::Point2f, 4> destination = {
          cv::Point2f(0, 0), cv::Point2f(static_cast<float>(width), 0),
          cv::Point2f(static_cast<float>(width), static_cast<float>(height)),
          cv::Point2f(0, static_cast<float>(height))};
      const auto transform = cv::getPerspectiveTransform(source, destination);
      cv::Mat crop;
      cv::warpPerspective(bgr, crop, transform, cv::Size(static_cast<int>(width),
                                                         static_cast<int>(height)),
                          cv::INTER_CUBIC, cv::BORDER_REPLICATE);
      if (static_cast<double>(crop.rows) / crop.cols >= config.tall_line_ratio) {
        cv::Mat rotated;
        cv::rotate(crop, rotated, cv::ROTATE_90_COUNTERCLOCKWISE);
        crop = std::move(rotated);
      }
      crops.push_back(std::move(crop));
    }
    return Result<std::vector<cv::Mat>>::success(std::move(crops));
  } catch (const cv::Exception& exception) {
    return Result<std::vector<cv::Mat>>::failure(
        Error{ErrorCode::postprocess_failed, "OpenCV failed while cropping a text region", exception.err});
  } catch (const std::exception& exception) {
    return Result<std::vector<cv::Mat>>::failure(
        Error{ErrorCode::internal_error, "Unexpected text crop failure", exception.what()});
  } catch (...) {
    return Result<std::vector<cv::Mat>>::failure(
        Error{ErrorCode::internal_error, "Unknown text crop failure", {}});
  }
}

}  // namespace light_ocr::internal
