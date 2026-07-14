#pragma once

#include <filesystem>
#include <vector>

#include "light_ocr/core.hpp"

namespace light_ocr::tools {

std::vector<BundleFile> load_bundle_directory(const std::filesystem::path& root);
std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path);

}  // namespace light_ocr::tools
