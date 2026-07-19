#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

#include "light_ocr/types.hpp"

namespace light_ocr::tools {

struct Arguments {
  std::filesystem::path bundle;
  std::filesystem::path pixels;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::size_t stride = 0;
  PixelFormat format = PixelFormat::bgr8;
  std::uint32_t warmup = 2;
  std::uint32_t iterations = 10;
  std::filesystem::path report;
  std::string profile;
  std::uint32_t target_width = 0;
  std::uint32_t target_height = 0;
  std::uint64_t maximum_peak_bytes = 0;
  std::uint32_t minimum_boxes = 0;
  std::optional<std::uint32_t> maximum_boxes;
  bool diagnostics = false;
  bool reuse_engine = false;
  std::string diagnostics_mode = "on";
};

inline std::uint64_t parse_unsigned(const std::string& value, const char* name) {
  std::size_t consumed = 0;
  const auto parsed = std::stoull(value, &consumed);
  if (consumed != value.size()) throw std::runtime_error(std::string("invalid ") + name);
  return parsed;
}

inline PixelFormat parse_format(const std::string& value) {
  if (value == "gray8") return PixelFormat::gray8;
  if (value == "rgb8") return PixelFormat::rgb8;
  if (value == "bgr8") return PixelFormat::bgr8;
  if (value == "rgba8") return PixelFormat::rgba8;
  throw std::runtime_error("format must be gray8, rgb8, bgr8, or rgba8");
}

inline EngineOptions engine_options_for_profile(const std::string& profile) {
  EngineOptions options;
  if (profile == "cpu_fast") {
    options.execution.provider = ExecutionProvider::cpu;
    options.intra_op_threads = std::max(
        1u, std::min(12u, std::thread::hardware_concurrency()));
  } else if (profile == "upstream_exact") {
    options.execution.provider = ExecutionProvider::cpu;
    options.detection.strategy = DetectionStrategy::upstream_exact;
    options.recognition_batch_size = 8;
  } else if (profile == "tiled_v1") {
    options.execution.provider = ExecutionProvider::cpu;
    options.detection.strategy = DetectionStrategy::tiled;
  } else if (profile == "apple_interactive" ||
             profile == "apple_strict" ||
             profile == "apple_cpu_fallback") {
    options.execution.provider = ExecutionProvider::apple;
    options.execution.session_fallback =
        profile == "apple_cpu_fallback" ? SessionFallback::cpu
                                         : SessionFallback::error;
    options.execution.cpu_partition =
        profile == "apple_strict" ? CpuPartition::forbid
                                   : CpuPartition::allow;
    options.execution.precision = Precision::fp16;
    options.detection.strategy = DetectionStrategy::bounded;
    options.recognition_batch_size = 1;
  } else if (profile == "webgpu_allow" || profile == "webgpu_strict") {
    options.execution.provider = ExecutionProvider::webgpu;
    options.execution.cpu_partition =
        profile == "webgpu_strict" ? CpuPartition::forbid
                                    : CpuPartition::allow;
    options.execution.precision = Precision::fp32;
    options.detection.strategy = DetectionStrategy::bounded;
    options.recognition_batch_size = 1;
  }
  return options;
}

inline Arguments parse_arguments(int argc, char** argv, bool benchmark) {
  Arguments result;
  for (int index = 1; index < argc; ++index) {
    const std::string option = argv[index];
    if (option == "--diagnostics") {
      result.diagnostics = true;
      continue;
    }
    if (option == "--reuse-engine") {
      result.reuse_engine = true;
      continue;
    }
    if (index + 1 >= argc) throw std::runtime_error("missing value for " + option);
    const std::string value = argv[++index];
    if (option == "--bundle") result.bundle = value;
    else if (option == "--pixels") result.pixels = value;
    else if (option == "--width") result.width = static_cast<std::uint32_t>(parse_unsigned(value, "width"));
    else if (option == "--height") result.height = static_cast<std::uint32_t>(parse_unsigned(value, "height"));
    else if (option == "--stride") result.stride = static_cast<std::size_t>(parse_unsigned(value, "stride"));
    else if (option == "--format") result.format = parse_format(value);
    else if (option == "--profile") result.profile = value;
    else if (benchmark && option == "--diagnostics-mode") result.diagnostics_mode = value;
    else if (benchmark && option == "--warmup") result.warmup = static_cast<std::uint32_t>(parse_unsigned(value, "warmup"));
    else if (benchmark && option == "--iterations") result.iterations = static_cast<std::uint32_t>(parse_unsigned(value, "iterations"));
    else if (benchmark && option == "--report") result.report = value;
    else if (benchmark && option == "--target-width") result.target_width = static_cast<std::uint32_t>(parse_unsigned(value, "target-width"));
    else if (benchmark && option == "--target-height") result.target_height = static_cast<std::uint32_t>(parse_unsigned(value, "target-height"));
    else if (benchmark && option == "--maximum-peak-bytes") result.maximum_peak_bytes = parse_unsigned(value, "maximum-peak-bytes");
    else if (benchmark && option == "--minimum-boxes") result.minimum_boxes = static_cast<std::uint32_t>(parse_unsigned(value, "minimum-boxes"));
    else if (benchmark && option == "--maximum-boxes") result.maximum_boxes = static_cast<std::uint32_t>(parse_unsigned(value, "maximum-boxes"));
    else throw std::runtime_error("unknown option: " + option);
  }
  if (result.bundle.empty() || result.pixels.empty() || result.width == 0 || result.height == 0 ||
      result.stride == 0 || (benchmark && result.iterations == 0)) {
    throw std::runtime_error(
        "required: --bundle DIR --pixels FILE --width N --height N --stride N --format FORMAT");
  }
  if (result.profile.empty()) {
    result.profile = benchmark ? "runtime_default" : "upstream_exact";
  }
  if (result.profile != "upstream_exact" && result.profile != "cpu_fast" &&
      result.profile != "bounded_default" && result.profile != "runtime_default" &&
      result.profile != "tiled_v1" && result.profile != "apple_interactive" &&
      result.profile != "apple_strict" &&
      result.profile != "apple_cpu_fallback" &&
      result.profile != "webgpu_allow" &&
      result.profile != "webgpu_strict") {
    throw std::runtime_error(
        "profile must be upstream_exact, cpu_fast, bounded_default, runtime_default, "
        "tiled_v1, apple_interactive, apple_strict, apple_cpu_fallback, "
        "webgpu_allow, or webgpu_strict");
  }
  if (result.diagnostics_mode != "on" && result.diagnostics_mode != "off") {
    throw std::runtime_error("diagnostics-mode must be on or off");
  }
  return result;
}

}  // namespace light_ocr::tools
