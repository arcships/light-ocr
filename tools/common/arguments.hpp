#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

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
  bool diagnostics = false;
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

inline Arguments parse_arguments(int argc, char** argv, bool benchmark) {
  Arguments result;
  for (int index = 1; index < argc; ++index) {
    const std::string option = argv[index];
    if (option == "--diagnostics") {
      result.diagnostics = true;
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
    else if (benchmark && option == "--warmup") result.warmup = static_cast<std::uint32_t>(parse_unsigned(value, "warmup"));
    else if (benchmark && option == "--iterations") result.iterations = static_cast<std::uint32_t>(parse_unsigned(value, "iterations"));
    else if (benchmark && option == "--report") result.report = value;
    else throw std::runtime_error("unknown option: " + option);
  }
  if (result.bundle.empty() || result.pixels.empty() || result.width == 0 || result.height == 0 ||
      result.stride == 0 || (benchmark && result.iterations == 0)) {
    throw std::runtime_error(
        "required: --bundle DIR --pixels FILE --width N --height N --stride N --format FORMAT");
  }
  return result;
}

}  // namespace light_ocr::tools
