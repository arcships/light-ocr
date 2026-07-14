#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "light_ocr/core.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size == 0) return 0;
  const auto file_count = std::min<std::size_t>((data[0] % 8) + 1, size);
  std::size_t cursor = 1;
  std::vector<light_ocr::BundleFile> files;
  files.reserve(file_count);
  for (std::size_t index = 0; index < file_count && cursor < size; ++index) {
    const auto requested_path_size = data[cursor] % 32;
    ++cursor;
    const auto path_size = std::min<std::size_t>(requested_path_size, size - cursor);
    std::string path(reinterpret_cast<const char*>(data + cursor), path_size);
    cursor += path_size;
    if (path.empty() && index == 0) path = "manifest.json";
    const auto remaining_files = file_count - index;
    const auto payload_size = remaining_files == 0 ? 0 : (size - cursor) / remaining_files;
    auto payload = std::make_shared<std::vector<std::uint8_t>>(
        data + cursor, data + cursor + payload_size);
    cursor += payload_size;
    files.push_back(light_ocr::BundleFile{std::move(path), std::move(payload)});
  }
  (void)light_ocr::ModelBundle::create(std::move(files));
  return 0;
}
