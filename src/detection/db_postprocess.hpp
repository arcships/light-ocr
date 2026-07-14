#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "light_ocr/error.hpp"
#include "light_ocr/types.hpp"
#include "model/bundle_data.hpp"

namespace light_ocr::internal {

struct DetectionCandidateTrace {
  std::uint32_t candidate_index = 0;
  Quad initial_quad;
  std::optional<float> score;
  std::vector<Point> expanded_polygon;
  std::optional<Quad> expanded_quad;
  std::optional<Quad> restored_quad;
  std::string decision;
};

struct DetectionBoxes {
  std::vector<Quad> boxes;
  std::uint32_t contour_candidates = 0;
  std::string threshold_bitmap_sha256;
  std::vector<DetectionCandidateTrace> traces;
};

Result<DetectionBoxes> db_postprocess(const float* probabilities, std::size_t element_count,
                                      const std::vector<std::int64_t>& shape,
                                      std::uint32_t original_width,
                                      std::uint32_t original_height,
                                      const DetectionConfig& config,
                                      const ResourceLimits& limits,
                                      bool include_trace = false);

}  // namespace light_ocr::internal
