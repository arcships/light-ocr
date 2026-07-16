#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "common/arguments.hpp"
#include "common/bundle_files.hpp"
#include "common/process_memory.hpp"
#include "light_ocr/core.hpp"
#include "util/sha256.hpp"

namespace {

std::unique_ptr<light_ocr::Engine> create_engine(
    const std::vector<light_ocr::BundleFile>& files,
    const light_ocr::EngineOptions& options) {
  auto bundle = light_ocr::ModelBundle::create(files);
  if (!bundle) throw std::runtime_error(bundle.error().message + ": " + bundle.error().detail);
  auto engine = light_ocr::Engine::create(std::move(bundle).value(), options);
  if (!engine) throw std::runtime_error(engine.error().message + ": " + engine.error().detail);
  return std::move(engine).value();
}

void run_page(light_ocr::Engine* engine,
              const light_ocr::ImageView& image) {
  auto result = engine->recognize(image);
  if (!result) throw std::runtime_error(result.error().message + ": " + result.error().detail);
}

light_ocr::EngineInfo run_cycle(
    const std::vector<light_ocr::BundleFile>& files,
    const light_ocr::ImageView& image,
    const light_ocr::EngineOptions& options) {
  auto engine = create_engine(files, options);
  run_page(engine.get(), image);
  auto info = engine->info();
  engine->close();
  return info;
}

const char* provider_name(light_ocr::ExecutionProvider provider) {
  return provider == light_ocr::ExecutionProvider::apple ? "apple" : "cpu";
}

nlohmann::json session_identity(
    const light_ocr::SessionExecutionInfo& session) {
  return {{"modelSha256", session.model_sha256},
          {"qualificationId", session.qualification_id},
          {"deviceValidated", session.device_validated},
          {"sessionFallback", session.session_fallback}};
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto arguments = light_ocr::tools::parse_arguments(argc, argv, true);
    const auto files = light_ocr::tools::load_bundle_directory(arguments.bundle);
    const auto engine_options =
        light_ocr::tools::engine_options_for_profile(arguments.profile);
    auto pixels = light_ocr::tools::read_binary_file(arguments.pixels);
    const light_ocr::ImageView image{pixels.data(), pixels.size(), arguments.width,
                                     arguments.height, arguments.stride, arguments.format};
    std::vector<std::uint64_t> resident;
    resident.reserve(arguments.iterations);
    std::uint64_t baseline = 0;
    light_ocr::EngineInfo engine_info;
    if (arguments.reuse_engine) {
      auto engine = create_engine(files, engine_options);
      engine_info = engine->info();
      for (std::uint32_t index = 0; index < arguments.warmup; ++index) {
        run_page(engine.get(), image);
      }
      light_ocr::tools::release_unused_memory();
      baseline = light_ocr::tools::resident_memory_bytes();
      for (std::uint32_t index = 0; index < arguments.iterations; ++index) {
        run_page(engine.get(), image);
        light_ocr::tools::release_unused_memory();
        resident.push_back(light_ocr::tools::resident_memory_bytes());
      }
      engine->close();
    } else {
      for (std::uint32_t index = 0; index < arguments.warmup; ++index) {
        engine_info = run_cycle(files, image, engine_options);
      }
      light_ocr::tools::release_unused_memory();
      baseline = light_ocr::tools::resident_memory_bytes();
      for (std::uint32_t index = 0; index < arguments.iterations; ++index) {
        engine_info = run_cycle(files, image, engine_options);
        light_ocr::tools::release_unused_memory();
        resident.push_back(light_ocr::tools::resident_memory_bytes());
      }
    }
    const auto minmax = std::minmax_element(resident.begin(), resident.end());
    const auto growth = static_cast<std::int64_t>(resident.back()) -
                        static_cast<std::int64_t>(baseline);
    const auto per_cycle = growth > 0
                               ? growth / static_cast<std::int64_t>(arguments.iterations)
                               : 0;
    constexpr std::int64_t maximum_growth = 32ll * 1024 * 1024;
    constexpr std::int64_t maximum_per_cycle = 8ll * 1024 * 1024;
    const bool passed = growth <= maximum_growth && per_cycle <= maximum_per_cycle;
    auto report_json = nlohmann::json({
        {"schemaVersion", "1.0"}, {"ok", true}, {"passed", passed},
        {"profile", arguments.profile},
        {"modelBundleId", engine_info.model_bundle_id},
        {"execution",
         {{"requestedProvider",
           provider_name(engine_info.execution.requested_provider)},
          {"detection", session_identity(engine_info.execution.detection)},
          {"recognition", session_identity(engine_info.execution.recognition)}}},
        {"lifecycleMode", arguments.reuse_engine ? "pages" : "engineCycles"},
        {"warmupCycles", arguments.warmup}, {"measuredCycles", arguments.iterations},
        {"residentBytes", {{"baseline", baseline}, {"minimum", *minmax.first},
                           {"maximum", *minmax.second}, {"final", resident.back()},
                           {"growth", growth}, {"growthPerCycle", per_cycle},
                           {"peak", light_ocr::tools::peak_resident_memory_bytes()}}},
        {"gate", {{"maximumGrowthBytes", maximum_growth},
                  {"maximumGrowthPerCycleBytes", maximum_per_cycle}}}});
    const auto canonical = report_json.dump();
    report_json["reportSha256"] = light_ocr::internal::sha256_hex(
        reinterpret_cast<const std::uint8_t*>(canonical.data()), canonical.size());
    const auto report = report_json.dump() + "\n";
    if (!arguments.report.empty()) {
      const auto parent = arguments.report.parent_path();
      if (!parent.empty()) std::filesystem::create_directories(parent);
      std::ofstream stream(arguments.report, std::ios::binary | std::ios::trunc);
      stream.exceptions(std::ios::badbit | std::ios::failbit);
      stream << report;
    }
    std::cout << report;
    return passed ? 0 : 1;
  } catch (const std::exception& exception) {
    std::cout << nlohmann::json({{"schemaVersion", "1.0"}, {"ok", false},
                                 {"error", exception.what()}}).dump()
              << '\n';
    return 2;
  }
}
