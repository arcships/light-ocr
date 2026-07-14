#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "common/arguments.hpp"
#include "common/bundle_files.hpp"
#include "common/process_memory.hpp"
#include "light_ocr/core.hpp"

namespace {

using Samples = std::vector<std::uint64_t>;

nlohmann::json distribution(Samples values) {
  std::sort(values.begin(), values.end());
  const auto percentile = [&values](double value) {
    const auto index = static_cast<std::size_t>(
        std::min<double>(values.size() - 1, std::ceil(value * values.size()) - 1));
    return values[index];
  };
  return {{"minimum", values.front()}, {"median", percentile(0.5)},
          {"p95", percentile(0.95)}, {"maximum", values.back()}};
}

std::uint64_t elapsed_us(std::chrono::steady_clock::time_point begin,
                         std::chrono::steady_clock::time_point end) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count());
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto arguments = light_ocr::tools::parse_arguments(argc, argv, true);
    const auto load_begin = std::chrono::steady_clock::now();
    auto files = light_ocr::tools::load_bundle_directory(arguments.bundle);
    std::uint64_t model_bundle_bytes = 0;
    for (const auto& file : files) model_bundle_bytes += file.bytes->size();
    auto bundle = light_ocr::ModelBundle::create(std::move(files));
    if (!bundle) throw std::runtime_error(bundle.error().message + ": " + bundle.error().detail);
    const auto load_end = std::chrono::steady_clock::now();

    const auto initialize_begin = std::chrono::steady_clock::now();
    auto engine = light_ocr::Engine::create(std::move(bundle).value());
    if (!engine) throw std::runtime_error(engine.error().message + ": " + engine.error().detail);
    const auto initialize_end = std::chrono::steady_clock::now();

    auto pixels = light_ocr::tools::read_binary_file(arguments.pixels);
    const light_ocr::ImageView image{pixels.data(), pixels.size(), arguments.width,
                                     arguments.height, arguments.stride, arguments.format};
    for (std::uint32_t index = 0; index < arguments.warmup; ++index) {
      auto result = engine.value()->recognize(image);
      if (!result) throw std::runtime_error(result.error().message + ": " + result.error().detail);
    }

    Samples wall;
    Samples total;
    Samples inference_only;
    std::array<Samples, 8> stages;
    Samples resident;
    wall.reserve(arguments.iterations);
    total.reserve(arguments.iterations);
    inference_only.reserve(arguments.iterations);
    resident.reserve(arguments.iterations);
    for (auto& samples : stages) samples.reserve(arguments.iterations);

    for (std::uint32_t index = 0; index < arguments.iterations; ++index) {
      const auto begin = std::chrono::steady_clock::now();
      auto result = engine.value()->recognize(image);
      const auto end = std::chrono::steady_clock::now();
      if (!result) throw std::runtime_error(result.error().message + ": " + result.error().detail);
      const auto& timing = result.value().timing;
      wall.push_back(elapsed_us(begin, end));
      total.push_back(timing.total_us);
      inference_only.push_back(timing.detection_inference_us + timing.recognition_inference_us);
      stages[0].push_back(timing.input_validation_us);
      stages[1].push_back(timing.detection_preprocess_us);
      stages[2].push_back(timing.detection_inference_us);
      stages[3].push_back(timing.detection_postprocess_us);
      stages[4].push_back(timing.crop_and_sort_us);
      stages[5].push_back(timing.recognition_preprocess_us);
      stages[6].push_back(timing.recognition_inference_us);
      stages[7].push_back(timing.recognition_postprocess_us);
      resident.push_back(light_ocr::tools::resident_memory_bytes());
    }

    constexpr std::array<std::string_view, 8> stage_names = {
        "inputValidation", "detectionPreprocess", "detectionInference",
        "detectionPostprocess", "cropAndSort", "recognitionPreprocess",
        "recognitionInference", "recognitionPostprocess"};
    nlohmann::json stage_report = nlohmann::json::object();
    for (std::size_t index = 0; index < stages.size(); ++index) {
      stage_report[stage_names[index]] = distribution(std::move(stages[index]));
    }
    const auto resident_minmax = std::minmax_element(resident.begin(), resident.end());
    const auto model_bundle_id = engine.value()->info().model_bundle_id;
    engine.value()->close();

    std::cout << nlohmann::json({
        {"schemaVersion", "1.0"}, {"ok", true}, {"backend", "native-cpp"},
        {"modelBundleId", model_bundle_id}, {"modelBundleBytes", model_bundle_bytes},
        {"loadUs", elapsed_us(load_begin, load_end)},
        {"engineInitializationUs", elapsed_us(initialize_begin, initialize_end)},
        {"warmup", arguments.warmup}, {"iterations", arguments.iterations},
        {"latencyUs", distribution(std::move(wall))},
        {"reportedTotalUs", distribution(std::move(total))},
        {"inferenceOnlyUs", distribution(std::move(inference_only))},
        {"stagesUs", std::move(stage_report)},
        {"memoryBytes", {{"residentMinimum", *resident_minmax.first},
                         {"residentMaximum", *resident_minmax.second},
                         {"residentFinal", resident.back()},
                         {"peakResident", light_ocr::tools::peak_resident_memory_bytes()}}}})
                     .dump()
              << '\n';
    return 0;
  } catch (const std::exception& exception) {
    std::cout << nlohmann::json({{"schemaVersion", "1.0"}, {"ok", false},
                                 {"error", exception.what()}}).dump()
              << '\n';
    return 2;
  }
}
