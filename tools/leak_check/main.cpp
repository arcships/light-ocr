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

namespace {

void run_cycle(const std::vector<light_ocr::BundleFile>& files,
               const light_ocr::ImageView& image) {
  auto bundle = light_ocr::ModelBundle::create(files);
  if (!bundle) throw std::runtime_error(bundle.error().message + ": " + bundle.error().detail);
  auto engine = light_ocr::Engine::create(std::move(bundle).value());
  if (!engine) throw std::runtime_error(engine.error().message + ": " + engine.error().detail);
  auto result = engine.value()->recognize(image);
  if (!result) throw std::runtime_error(result.error().message + ": " + result.error().detail);
  engine.value()->close();
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto arguments = light_ocr::tools::parse_arguments(argc, argv, true);
    const auto files = light_ocr::tools::load_bundle_directory(arguments.bundle);
    auto pixels = light_ocr::tools::read_binary_file(arguments.pixels);
    const light_ocr::ImageView image{pixels.data(), pixels.size(), arguments.width,
                                     arguments.height, arguments.stride, arguments.format};
    for (std::uint32_t index = 0; index < arguments.warmup; ++index) run_cycle(files, image);

    const auto baseline = light_ocr::tools::resident_memory_bytes();
    std::vector<std::uint64_t> resident;
    resident.reserve(arguments.iterations);
    for (std::uint32_t index = 0; index < arguments.iterations; ++index) {
      run_cycle(files, image);
      resident.push_back(light_ocr::tools::resident_memory_bytes());
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
    const auto report = nlohmann::json({
        {"schemaVersion", "1.0"}, {"ok", true}, {"passed", passed},
        {"warmupCycles", arguments.warmup}, {"measuredCycles", arguments.iterations},
        {"residentBytes", {{"baseline", baseline}, {"minimum", *minmax.first},
                           {"maximum", *minmax.second}, {"final", resident.back()},
                           {"growth", growth}, {"growthPerCycle", per_cycle},
                           {"peak", light_ocr::tools::peak_resident_memory_bytes()}}},
        {"gate", {{"maximumGrowthBytes", maximum_growth},
                  {"maximumGrowthPerCycleBytes", maximum_per_cycle}}}}).dump() + "\n";
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
