#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <opencv2/core/utils/logger.hpp>

#include "common/arguments.hpp"
#include "common/bundle_files.hpp"
#include "detection/db_postprocess.hpp"
#include "detection/tiled.hpp"
#include "geometry/geometry.hpp"
#include "inference/onnxruntime/backend.hpp"
#include "light_ocr/core.hpp"
#include "model/bundle_data.hpp"
#include "preprocess/image.hpp"
#include "preprocess/tensor.hpp"
#include "recognition/ctc_decode.hpp"
#include "util/sha256.hpp"

namespace light_ocr::internal {
namespace {

using Json = nlohmann::json;

template <class T>
T checked(Result<T> result, const char* stage) {
  if (!result) {
    throw std::runtime_error(std::string(stage) + ": " + to_string(result.error().code) +
                             ": " + result.error().message + ": " +
                             result.error().detail);
  }
  return std::move(result).value();
}

Json shape_json(const std::vector<std::int64_t>& shape) {
  Json result = Json::array();
  for (const auto dimension : shape) result.push_back(dimension);
  return result;
}

Json samples_json(const float* values, std::size_t size) {
  Json result = Json::array();
  if (size == 0) return result;
  std::vector<std::size_t> indices = {
      0, size / 4, size / 2, (size * 3) / 4, size - 1};
  std::sort(indices.begin(), indices.end());
  indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
  for (const auto index : indices) result.push_back({{"index", index}, {"value", values[index]}});
  return result;
}

std::string float_hash(const float* values, std::size_t size) {
  return sha256_hex(reinterpret_cast<const std::uint8_t*>(values),
                    size * sizeof(float));
}

Json samples_json(const std::vector<float>& values) {
  return samples_json(values.data(), values.size());
}

std::string float_hash(const std::vector<float>& values) {
  return float_hash(values.data(), values.size());
}

std::vector<std::uint8_t> mat_bytes(const cv::Mat& matrix) {
  std::vector<std::uint8_t> packed;
  const auto row_bytes = static_cast<std::size_t>(matrix.cols) * matrix.elemSize();
  packed.reserve(row_bytes * static_cast<std::size_t>(matrix.rows));
  for (int row = 0; row < matrix.rows; ++row) {
    const auto* begin = matrix.ptr<std::uint8_t>(row);
    packed.insert(packed.end(), begin, begin + row_bytes);
  }
  return packed;
}

std::string base64_encode(const std::vector<std::uint8_t>& values) {
  static constexpr char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string output;
  output.reserve(((values.size() + 2) / 3) * 4);
  for (std::size_t index = 0; index < values.size(); index += 3) {
    const std::uint32_t first = values[index];
    const std::uint32_t second = index + 1 < values.size() ? values[index + 1] : 0;
    const std::uint32_t third = index + 2 < values.size() ? values[index + 2] : 0;
    const std::uint32_t combined = (first << 16) | (second << 8) | third;
    output.push_back(alphabet[(combined >> 18) & 0x3f]);
    output.push_back(alphabet[(combined >> 12) & 0x3f]);
    output.push_back(index + 1 < values.size() ? alphabet[(combined >> 6) & 0x3f] : '=');
    output.push_back(index + 2 < values.size() ? alphabet[combined & 0x3f] : '=');
  }
  return output;
}

Json quad_json(const Quad& quad) {
  Json result = Json::array();
  for (const auto& point : quad.points) result.push_back({point.x, point.y});
  return result;
}

Json points_json(const std::vector<Point>& points) {
  Json result = Json::array();
  for (const auto& point : points) result.push_back({point.x, point.y});
  return result;
}

Json detection_trace_json(const DetectionCandidateTrace& trace) {
  return {{"candidateIndex", trace.candidate_index},
          {"initialQuad", quad_json(trace.initial_quad)},
          {"score", trace.score ? Json(*trace.score) : Json(nullptr)},
          {"expandedPolygon", points_json(trace.expanded_polygon)},
          {"expandedQuad", trace.expanded_quad ? quad_json(*trace.expanded_quad)
                                                 : Json(nullptr)},
          {"restoredQuad", trace.restored_quad ? quad_json(*trace.restored_quad)
                                                 : Json(nullptr)},
          {"decision", trace.decision}};
}

Json decoded_json(const DecodedText& decoded) {
  return {{"text", decoded.text},
          {"confidence", decoded.confidence},
          {"selectedIndices", decoded.selected_indices},
          {"selectedProbabilities", decoded.selected_probabilities}};
}

Json tile_json(const TileRect& tile) {
  return {{"ordinal", tile.ordinal}, {"x", tile.x}, {"y", tile.y},
          {"width", tile.width}, {"height", tile.height}};
}

Json tiled_candidate_json(const TiledCandidate& candidate) {
  Json edges = Json::array();
  if ((candidate.nearby_artificial_edges & artificial_left) != 0) edges.push_back("left");
  if ((candidate.nearby_artificial_edges & artificial_top) != 0) edges.push_back("top");
  if ((candidate.nearby_artificial_edges & artificial_right) != 0) edges.push_back("right");
  if ((candidate.nearby_artificial_edges & artificial_bottom) != 0) edges.push_back("bottom");
  return {{"quad", quad_json(candidate.global_quad)},
          {"score", candidate.db_score},
          {"tileOrdinal", candidate.tile_ordinal},
          {"candidateOrdinal", candidate.candidate_ordinal},
          {"sourceTile", tile_json(candidate.source_tile)},
          {"nearbyArtificialEdges", std::move(edges)},
          {"distanceToNearestArtificialEdge",
           candidate.distance_to_nearest_artificial_edge}};
}

Json run_tiled_probe(const BundleData& data, OnnxSession& detection_session,
                     OnnxSession& recognition_session,
                     const ValidatedImage& validated, const ImageView& image) {
  if (!data.tiled_detection) {
    throw std::runtime_error("tiled-v1 is unavailable in this bundle");
  }
  auto tiles = checked(plan_detection_tiles(image.width, image.height,
                                             *data.tiled_detection,
                                             data.limits.max_detection_tiles),
                       "tile plan");
  Json passes = Json::array();
  Json raw_candidate_records = Json::array();
  std::vector<TiledCandidate> raw_candidates;
  for (const auto& tile : tiles) {
    const cv::Mat roi = validated.bgr(cv::Rect(
        static_cast<int>(tile.x), static_cast<int>(tile.y),
        static_cast<int>(tile.width), static_cast<int>(tile.height)));
    auto detection_input = checked(
        make_detection_input(roi, data.detection, DetectionStrategy::bounded,
                             data.tiled_detection->tile_side, data.limits),
        "tiled detection preprocess");
    auto detection_output = checked(
        detection_session.run(detection_input.values, detection_input.shape),
        "tiled detection inference");
    auto detected = checked(
        db_postprocess(detection_output.data(), detection_output.size(),
                       detection_output.shape(), tile.width, tile.height,
                       data.detection, data.limits, true, true),
        "tiled detection postprocess");
    if (detected.boxes.size() != detected.scores.size()) {
      throw std::runtime_error("tiled stage probe box/score count mismatch");
    }
    Json accepted = Json::array();
    for (std::size_t index = 0; index < detected.boxes.size(); ++index) {
      auto candidate = checked(
          make_tiled_candidate(
              detected.boxes[index], detected.scores[index], tile,
              image.width, image.height, static_cast<std::uint32_t>(index),
              data.tiled_detection->artificial_boundary_margin),
          "tiled candidate restore");
      accepted.push_back({{"tileOrdinal", candidate.tile_ordinal},
                          {"candidateOrdinal", candidate.candidate_ordinal}});
      raw_candidate_records.push_back(tiled_candidate_json(candidate));
      raw_candidates.push_back(std::move(candidate));
    }
    Json trace_records = Json::array();
    for (const auto& trace : detected.traces) {
      trace_records.push_back(detection_trace_json(trace));
    }
    passes.push_back(
        {{"tileOrdinal", tile.ordinal},
         {"roi", {tile.x, tile.y, tile.width, tile.height}},
         {"detectionInput",
          {{"shape", shape_json(detection_input.shape)},
           {"sha256Float32LE", float_hash(detection_input.values)},
           {"samples", samples_json(detection_input.values)}}},
         {"detectionOutput",
          {{"shape", shape_json(detection_output.shape())},
           {"sha256Float32LE",
            float_hash(detection_output.data(), detection_output.size())},
           {"samples",
            samples_json(detection_output.data(), detection_output.size())}}},
         {"contourCandidates", detected.contour_candidates},
         {"thresholdBitmapSha256", detected.threshold_bitmap_sha256},
         {"detectionCandidates", std::move(trace_records)},
         {"acceptedCandidates", std::move(accepted)}});
  }

  auto merged = checked(merge_tiled_candidates(std::move(raw_candidates),
                                                *data.tiled_detection),
                        "tiled merge");
  Json suppressions = Json::array();
  for (const auto& suppression : merged.suppressions) {
    suppressions.push_back(
        {{"candidate", {suppression.candidate_tile_ordinal,
                         suppression.candidate_ordinal}},
         {"representative", {suppression.representative_tile_ordinal,
                              suppression.representative_ordinal}}});
  }
  Json representatives = Json::array();
  std::vector<Quad> boxes;
  boxes.reserve(merged.representatives.size());
  for (const auto& representative : merged.representatives) {
    representatives.push_back({representative.tile_ordinal,
                               representative.candidate_ordinal});
    boxes.push_back(representative.global_quad);
  }
  auto sorted_boxes = sort_reading_order(std::move(boxes), data.geometry);
  auto crops = checked(
      crop_text_regions(validated.bgr, sorted_boxes, data.geometry, data.limits),
      "tiled crop");
  auto plans = checked(
      plan_recognition_batches(sorted_boxes, data.geometry, data.recognition,
                               data.recognition.default_batch_size, data.limits),
      "tiled recognition plan");

  Json output = {
      {"schemaVersion", "1.0"},
      {"profile", "tiled_v1"},
      {"modelBundleId", data.id},
      {"image", {{"width", image.width}, {"height", image.height}}},
      {"models",
       {{"detection",
         {{"inputName", detection_session.input_name()},
          {"outputName", detection_session.output_name()}}},
        {"recognition",
         {{"inputName", recognition_session.input_name()},
          {"outputName", recognition_session.output_name()}}}}},
      {"contractVersion", data.tiled_detection->contract_version},
      {"detectionPasses", std::move(passes)},
      {"rawCandidates", std::move(raw_candidate_records)},
      {"suppressions", std::move(suppressions)},
      {"representatives", std::move(representatives)},
      {"boxes", Json::array()},
      {"crops", Json::array()},
      {"recognitionBatches", Json::array()},
      {"decoded", Json::array()},
      {"lines", Json::array()},
  };
  for (const auto& box : sorted_boxes) output["boxes"].push_back(quad_json(box));
  for (std::size_t index = 0; index < crops.size(); ++index) {
    const auto pixels = mat_bytes(crops[index]);
    output["crops"].push_back(
        {{"index", index},
         {"width", crops[index].cols},
         {"height", crops[index].rows},
         {"channels", crops[index].channels()},
         {"sha256Bgr8", sha256_hex(pixels.data(), pixels.size())},
         {"pixelsBgr8Base64", base64_encode(pixels)}});
  }

  std::vector<DecodedText> decoded(sorted_boxes.size());
  for (std::size_t batch_index = 0; batch_index < plans.size(); ++batch_index) {
    const auto& plan = plans[batch_index];
    std::vector<cv::Mat> batch_crops;
    batch_crops.reserve(plan.samples.size());
    for (const auto& sample : plan.samples) {
      batch_crops.push_back(crops[sample.input_index]);
    }
    auto batch = checked(
        make_recognition_batch(batch_crops, plan, data.recognition, data.limits),
        "tiled recognition preprocess");
    auto recognition_output = checked(
        recognition_session.run(batch.values, batch.shape),
        "tiled recognition inference");
    auto batch_decoded = checked(
        decode_ctc(recognition_output.data(), recognition_output.size(),
                   recognition_output.shape(), data.recognition.characters,
                   data.recognition.blank_index,
                   data.recognition.collapse_repeats),
        "tiled recognition decode");
    if (batch_decoded.size() != batch.input_indices.size()) {
      throw std::runtime_error("tiled recognition result count mismatch");
    }
    output["recognitionBatches"].push_back(
        {{"batchIndex", batch_index},
         {"inputIndices", batch.input_indices},
         {"inputShape", shape_json(batch.shape)},
         {"inputSha256Float32LE", float_hash(batch.values)},
         {"inputSamples", samples_json(batch.values)},
         {"outputShape", shape_json(recognition_output.shape())},
         {"outputSha256Float32LE",
          float_hash(recognition_output.data(), recognition_output.size())},
         {"outputSamples",
          samples_json(recognition_output.data(), recognition_output.size())}});
    for (std::size_t index = 0; index < batch.input_indices.size(); ++index) {
      decoded[batch.input_indices[index]] = batch_decoded[index];
    }
  }
  for (std::size_t index = 0; index < decoded.size(); ++index) {
    output["decoded"].push_back(decoded_json(decoded[index]));
    if (!decoded[index].text.empty() &&
        decoded[index].confidence >= data.recognition.default_score_threshold) {
      output["lines"].push_back(
          {{"text", decoded[index].text},
           {"confidence", decoded[index].confidence},
           {"box", quad_json(sorted_boxes[index])}});
    }
  }
  return output;
}

}  // namespace

class StageProbe {
 public:
  static Json run(const ModelBundle& bundle, const ImageView& image,
                  const std::string& profile = "upstream_exact") {
    if (!bundle.data_) throw std::runtime_error("validated bundle data is unavailable");
    const auto& data = *bundle.data_;
    const auto& detection_bytes = data.files.at(data.detection_model_path);
    const auto& recognition_bytes = data.files.at(data.recognition_model_path);
    InferenceSessionConfig detection_config;
    detection_config.model_id = data.detection_model_id;
    detection_config.model_sha256 = data.detection_model_sha256;
    detection_config.shape_policy = "dynamic";
    detection_config.qualification_id = "builtin-cpu-v1";
    auto recognition_config = detection_config;
    recognition_config.model_id = data.recognition_model_id;
    recognition_config.model_sha256 = data.recognition_model_sha256;
    auto detection_session = checked(
        OnnxSession::create(detection_bytes, detection_config,
                            ModelKind::detection),
        "detection session");
    auto recognition_session = checked(
        OnnxSession::create(recognition_bytes, recognition_config,
                            ModelKind::recognition,
                            data.recognition.characters.size() + 1),
        "recognition session");
    auto validated = checked(validate_and_convert_image(image, data.limits), "image");
    if (profile == "tiled_v1") {
      return run_tiled_probe(data, *detection_session, *recognition_session,
                             validated, image);
    }
    const auto upstream_exact = profile == "upstream_exact";
    if (!upstream_exact && profile != "bounded_default" &&
        profile != "runtime_default") {
      throw std::runtime_error("unsupported stage-probe profile: " + profile);
    }
    const auto detection_strategy =
        upstream_exact ? DetectionStrategy::upstream_exact
                       : data.default_detection_strategy;
    const auto detection_max_side =
        upstream_exact ? data.detection.max_side_limit
                       : data.default_detection_max_side;
    const auto batch_size = upstream_exact
                                ? data.recognition.maximum_batch_size
                                : data.recognition.default_batch_size;
    auto detection_input = checked(
        make_detection_input(validated.bgr, data.detection, detection_strategy,
                             detection_max_side, data.limits),
        "detection preprocess");
    auto detection_output =
        checked(detection_session->run(detection_input.values, detection_input.shape),
                "detection inference");
    auto detected = checked(
        db_postprocess(detection_output.data(), detection_output.size(),
                       detection_output.shape(), image.width, image.height, data.detection,
                       data.limits, true),
        "detection postprocess");
    const auto sorted_boxes = sort_reading_order(detected.boxes, data.geometry);
    auto crops =
        checked(crop_text_regions(validated.bgr, sorted_boxes, data.geometry, data.limits),
                "crop");
    auto plans = checked(
        plan_recognition_batches(sorted_boxes, data.geometry, data.recognition,
                                 batch_size, data.limits),
        "recognition plan");

    Json output = {
        {"schemaVersion", "1.0"},
        {"modelBundleId", data.id},
        {"image", {{"width", image.width}, {"height", image.height}}},
        {"models",
         {{"detection",
           {{"inputName", detection_session->input_name()},
            {"outputName", detection_session->output_name()}}},
          {"recognition",
           {{"inputName", recognition_session->input_name()},
            {"outputName", recognition_session->output_name()}}}}},
        {"detectionInput",
         {{"shape", shape_json(detection_input.shape)},
          {"sha256Float32LE", float_hash(detection_input.values)},
          {"samples", samples_json(detection_input.values)}}},
        {"detectionOutput",
         {{"shape", shape_json(detection_output.shape())},
          {"sha256Float32LE", float_hash(detection_output.data(), detection_output.size())},
          {"samples", samples_json(detection_output.data(), detection_output.size())}}},
        {"contourCandidates", detected.contour_candidates},
        {"thresholdBitmapSha256", detected.threshold_bitmap_sha256},
        {"detectionCandidates", Json::array()},
        {"boxes", Json::array()},
        {"crops", Json::array()},
        {"recognitionBatches", Json::array()},
        {"decoded", Json::array()},
        {"lines", Json::array()},
    };
    for (const auto& trace : detected.traces) {
      output["detectionCandidates"].push_back(detection_trace_json(trace));
    }
    for (const auto& box : sorted_boxes) output["boxes"].push_back(quad_json(box));
    for (std::size_t index = 0; index < crops.size(); ++index) {
      const auto pixels = mat_bytes(crops[index]);
      output["crops"].push_back({{"index", index},
                                 {"width", crops[index].cols},
                                 {"height", crops[index].rows},
                                 {"channels", crops[index].channels()},
                                 {"sha256Bgr8", sha256_hex(pixels.data(), pixels.size())},
                                 {"pixelsBgr8Base64", base64_encode(pixels)}});
    }

    std::vector<DecodedText> decoded(sorted_boxes.size());
    for (std::size_t batch_index = 0; batch_index < plans.size(); ++batch_index) {
      const auto& plan = plans[batch_index];
      std::vector<cv::Mat> batch_crops;
      batch_crops.reserve(plan.samples.size());
      for (const auto& sample : plan.samples) {
        batch_crops.push_back(crops[sample.input_index]);
      }
      auto batch = checked(
          make_recognition_batch(batch_crops, plan, data.recognition, data.limits),
          "recognition preprocess");
      auto recognition_output =
          checked(recognition_session->run(batch.values, batch.shape), "recognition inference");
      auto batch_decoded = checked(
          decode_ctc(recognition_output.data(), recognition_output.size(),
                     recognition_output.shape(), data.recognition.characters,
                     data.recognition.blank_index, data.recognition.collapse_repeats),
          "recognition decode");
      if (batch_decoded.size() != batch.input_indices.size()) {
        throw std::runtime_error("recognition result count does not match batch");
      }
      Json batch_record = {
          {"batchIndex", batch_index},
          {"inputIndices", batch.input_indices},
          {"inputShape", shape_json(batch.shape)},
          {"inputSha256Float32LE", float_hash(batch.values)},
          {"inputSamples", samples_json(batch.values)},
          {"outputShape", shape_json(recognition_output.shape())},
          {"outputSha256Float32LE", float_hash(recognition_output.data(), recognition_output.size())},
          {"outputSamples", samples_json(recognition_output.data(), recognition_output.size())},
      };
      output["recognitionBatches"].push_back(std::move(batch_record));
      for (std::size_t index = 0; index < batch.input_indices.size(); ++index) {
        decoded[batch.input_indices[index]] = batch_decoded[index];
      }
    }
    for (std::size_t index = 0; index < decoded.size(); ++index) {
      output["decoded"].push_back(decoded_json(decoded[index]));
      if (!decoded[index].text.empty() &&
          decoded[index].confidence >= data.recognition.default_score_threshold) {
        output["lines"].push_back({{"text", decoded[index].text},
                                    {"confidence", decoded[index].confidence},
                                    {"box", quad_json(sorted_boxes[index])}});
      }
    }
    return output;
  }
};

}  // namespace light_ocr::internal

int main(int argc, char** argv) {
  try {
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_SILENT);
    const auto arguments = light_ocr::tools::parse_arguments(argc, argv, false);
    auto files = light_ocr::tools::load_bundle_directory(arguments.bundle);
    auto bundle = light_ocr::ModelBundle::create(std::move(files));
    if (!bundle) {
      throw std::runtime_error(std::string(light_ocr::to_string(bundle.error().code)) + ": " +
                               bundle.error().message + ": " + bundle.error().detail);
    }
    auto pixels = light_ocr::tools::read_binary_file(arguments.pixels);
    const light_ocr::ImageView image{pixels.data(), pixels.size(), arguments.width,
                                     arguments.height, arguments.stride, arguments.format};
    std::cout << light_ocr::internal::StageProbe::run(bundle.value(), image,
                                                      arguments.profile)
                     .dump()
              << '\n';
    return 0;
  } catch (const std::exception& exception) {
    std::cout << nlohmann::json({{"ok", false}, {"error", exception.what()}}).dump() << '\n';
    return 2;
  }
}
