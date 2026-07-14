#include "common/bundle_files.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace light_ocr::tools {

std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("cannot open file: " + path.string());
  stream.seekg(0, std::ios::end);
  const auto length = stream.tellg();
  if (length < 0) throw std::runtime_error("cannot determine file size: " + path.string());
  stream.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
  if (!bytes.empty()) {
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream) throw std::runtime_error("cannot read complete file: " + path.string());
  }
  return bytes;
}

std::vector<BundleFile> load_bundle_directory(const std::filesystem::path& root) {
  if (!std::filesystem::is_directory(root)) {
    throw std::runtime_error("bundle root is not a directory: " + root.string());
  }
  std::vector<std::filesystem::path> paths;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    if (entry.is_symlink()) throw std::runtime_error("bundle contains a symbolic link");
    if (entry.is_regular_file()) paths.push_back(entry.path());
  }
  std::sort(paths.begin(), paths.end());
  std::vector<BundleFile> files;
  files.reserve(paths.size());
  for (const auto& path : paths) {
    auto relative = std::filesystem::relative(path, root).generic_string();
    auto storage = std::make_shared<const std::vector<std::uint8_t>>(read_binary_file(path));
    files.push_back(BundleFile{std::move(relative), std::move(storage)});
  }
  return files;
}

}  // namespace light_ocr::tools
