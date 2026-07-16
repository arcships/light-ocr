#include <exception>
#include <iostream>
#include <utility>

#include <nlohmann/json.hpp>

#include "common/arguments.hpp"
#include "common/bundle_files.hpp"
#include "light_ocr/core.hpp"

namespace {

nlohmann::json error_json(const light_ocr::Error& error) {
  return {{"ok", false}, {"error", {{"code", light_ocr::to_string(error.code)},
                                      {"message", error.message}, {"detail", error.detail}}}};
}

nlohmann::json result_json(const light_ocr::OcrResult& result) {
  nlohmann::json lines = nlohmann::json::array();
  for (const auto& line : result.lines) {
    nlohmann::json points = nlohmann::json::array();
    for (const auto& point : line.box.points) points.push_back({point.x, point.y});
    lines.push_back({{"text", line.text}, {"confidence", line.confidence}, {"box", points}});
  }
  nlohmann::json output = {
      {"ok", true}, {"image", {{"width", result.image_width}, {"height", result.image_height}}},
      {"modelBundleId", result.model_bundle_id}, {"lines", std::move(lines)},
      {"timingUs", {{"total", result.timing.total_us},
                    {"inputValidation", result.timing.input_validation_us},
                    {"detectionPreprocess", result.timing.detection_preprocess_us},
                    {"detectionInference", result.timing.detection_inference_us},
                    {"detectionPostprocess", result.timing.detection_postprocess_us},
                    {"detectionMerge", result.timing.detection_merge_us},
                    {"cropAndSort", result.timing.crop_and_sort_us},
                    {"recognitionPreprocess", result.timing.recognition_preprocess_us},
                    {"recognitionInference", result.timing.recognition_inference_us},
                    {"recognitionPostprocess", result.timing.recognition_postprocess_us}}}};
  if (result.diagnostics) {
    nlohmann::json passes = nlohmann::json::array();
    nlohmann::json recognition_shapes = nlohmann::json::array();
    nlohmann::json recognition_routes = nlohmann::json::array();
    for (const auto& pass : result.diagnostics->detection_passes) {
      passes.push_back({{"tileOrdinal", pass.tile_ordinal},
                        {"roi", {pass.x, pass.y, pass.width, pass.height}},
                        {"tensorShape", {1, 3, pass.tensor_height, pass.tensor_width}},
                        {"contourCandidates", pass.contour_candidates},
                        {"rawCandidates", pass.raw_candidates}});
    }
    for (const auto& shape : result.diagnostics->recognition_batch_shapes) {
      recognition_shapes.push_back(
          {shape.batch_size, 3, shape.height, shape.width});
      recognition_routes.push_back({
          {"tensorShape", {shape.batch_size, 3, shape.height, shape.width}},
          {"computeUnit", shape.compute_unit},
          {"modelId", shape.model_id},
          {"shapeBucket", shape.shape_bucket}});
    }
    output["diagnostics"] = {{"detectedCandidates", result.diagnostics->detected_candidates},
                             {"acceptedBoxes", result.diagnostics->accepted_boxes},
                             {"rawDetectionBoxes", result.diagnostics->raw_detection_boxes},
                             {"suppressedDuplicateBoxes",
                              result.diagnostics->suppressed_duplicate_boxes},
                             {"maxLiveDetectionPassBuffers",
                              result.diagnostics->max_live_detection_pass_buffers},
                             {"detectionPasses", std::move(passes)},
                             {"recognitionBatchShapes",
                              std::move(recognition_shapes)},
                             {"recognitionRoutes",
                              std::move(recognition_routes)},
                             {"rejectedLines", result.diagnostics->rejected_lines.size()}};
  }
  return output;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto arguments = light_ocr::tools::parse_arguments(argc, argv, false);
    auto bundle = light_ocr::ModelBundle::create(
        light_ocr::tools::load_bundle_directory(arguments.bundle));
    if (!bundle) {
      std::cout << error_json(bundle.error()).dump() << '\n';
      return 2;
    }
    auto engine = light_ocr::Engine::create(
        std::move(bundle).value(),
        light_ocr::tools::engine_options_for_profile(arguments.profile));
    if (!engine) {
      std::cout << error_json(engine.error()).dump() << '\n';
      return 2;
    }
    auto pixels = light_ocr::tools::read_binary_file(arguments.pixels);
    const light_ocr::ImageView image{pixels.data(), pixels.size(), arguments.width,
                                     arguments.height, arguments.stride, arguments.format};
    light_ocr::RecognizeOptions options;
    options.include_diagnostics = arguments.diagnostics;
    auto result = engine.value()->recognize(image, options);
    if (!result) {
      std::cout << error_json(result.error()).dump() << '\n';
      return 2;
    }
    std::cout << result_json(result.value()).dump() << '\n';
    return 0;
  } catch (const std::exception& exception) {
    std::cout << nlohmann::json({{"ok", false}, {"error", {{"code", "invalid_argument"},
                 {"message", exception.what()}}}}).dump() << '\n';
    return 2;
  }
}
