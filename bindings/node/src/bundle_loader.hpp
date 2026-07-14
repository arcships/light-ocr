#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include "light_ocr/core.hpp"

namespace light_ocr::node {

class BundleIoError final : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct LoadedBundle {
  std::vector<BundleFile> files;
  std::uint64_t total_bytes = 0;
};

LoadedBundle load_bundle_directory_secure(const std::filesystem::path& root);

}  // namespace light_ocr::node
