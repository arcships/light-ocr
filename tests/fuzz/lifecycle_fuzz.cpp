#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <vector>

#include "common/bundle_files.hpp"
#include "light_ocr/core.hpp"

namespace {

const std::optional<std::vector<light_ocr::BundleFile>>& bundle_files() {
  static const auto files = []() -> std::optional<std::vector<light_ocr::BundleFile>> {
    const char* path = std::getenv("LIGHT_OCR_MODEL_BUNDLE");
    if (path == nullptr || path[0] == '\0') return std::nullopt;
    try {
      return light_ocr::tools::load_bundle_directory(path);
    } catch (...) {
      return std::nullopt;
    }
  }();
  return files;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const auto& files = bundle_files();
  if (!files || size == 0) return 0;
  auto bundle = light_ocr::ModelBundle::create(*files);
  if (!bundle) return 0;
  light_ocr::EngineOptions engine_options;
  engine_options.intra_op_threads = 1;
  engine_options.inter_op_threads = 1;
  auto engine = light_ocr::Engine::create(std::move(bundle).value(), engine_options);
  if (!engine) return 0;

  const auto width = static_cast<std::uint32_t>((data[0] % 32) + 1);
  const auto height = static_cast<std::uint32_t>((size > 1 ? data[1] : data[0]) % 32 + 1);
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 3, 255);
  const light_ocr::ImageView image{pixels.data(), pixels.size(), width, height, width * 3,
                                   light_ocr::PixelFormat::bgr8};
  for (std::size_t index = 2; index < std::min<std::size_t>(size, 10); ++index) {
    switch (data[index] % 3) {
      case 0:
        (void)engine.value()->recognize(image);
        break;
      case 1:
        engine.value()->close();
        break;
      default:
        (void)engine.value()->info();
        break;
    }
  }
  engine.value()->close();
  return 0;
}
