#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "light_ocr/error.hpp"
#include "light_ocr/types.hpp"

namespace light_ocr {

using SharedBytes = std::shared_ptr<const std::vector<std::uint8_t>>;

struct BundleFile {
  std::string path;
  SharedBytes bytes;
};

namespace internal {
struct BundleData;
class EngineFactory;
class StageProbe;
}

class ModelBundle {
 public:
  static Result<ModelBundle> create(std::vector<BundleFile> files);

  ModelBundle(ModelBundle&&) noexcept;
  ModelBundle& operator=(ModelBundle&&) noexcept;
  ~ModelBundle();

  ModelBundle(const ModelBundle&) = delete;
  ModelBundle& operator=(const ModelBundle&) = delete;

  const std::string& id() const noexcept;
  const std::string& schema_version() const noexcept;

 private:
  explicit ModelBundle(std::shared_ptr<const internal::BundleData> data);
  std::shared_ptr<const internal::BundleData> data_;

  friend class Engine;
  friend class internal::EngineFactory;
  friend class internal::StageProbe;
};

class Engine {
 public:
  static Result<std::unique_ptr<Engine>> create(
      ModelBundle bundle, const EngineOptions& options = {});

  virtual ~Engine() noexcept;

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;
  Engine(Engine&&) = delete;
  Engine& operator=(Engine&&) = delete;

  virtual Result<OcrResult> recognize(
      const ImageView& image, const RecognizeOptions& options = {}) noexcept = 0;
  virtual const EngineInfo& info() const noexcept = 0;
  virtual void close() noexcept = 0;

 protected:
  Engine() = default;
};

}  // namespace light_ocr
