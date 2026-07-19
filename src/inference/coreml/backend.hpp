#pragma once

#include <memory>
#include <string>
#include <vector>

#include "inference/backend.hpp"

namespace light_ocr::internal {

bool coreml_device_available() noexcept;
bool coreml_device_has_neural_engine() noexcept;
std::string coreml_device_architecture() noexcept;
bool coreml_device_is_validated(
    const std::vector<std::string>& device_families) noexcept;
bool coreml_device_is_allowed(
    const std::string& device_policy,
    const std::vector<std::string>& architectures,
    const std::vector<std::string>& validated_device_families) noexcept;
std::string coreml_device_description() noexcept;

class CoreMlSession final : public InferenceSession {
 public:
  static Result<std::unique_ptr<CoreMlSession>> create(
      const InferenceSessionConfig& config, ModelKind kind,
      std::optional<CreationReason>* creation_reason = nullptr);

  ~CoreMlSession() noexcept override;

  Result<TensorOutput> run(const std::vector<float>& values,
                           const std::vector<std::int64_t>& shape) noexcept override;

  const SessionExecutionInfo& execution_info() const noexcept override {
    return execution_info_;
  }

 private:
  class Impl;

  CoreMlSession(std::unique_ptr<Impl> impl,
                SessionExecutionInfo execution_info);

  std::unique_ptr<Impl> impl_;
  SessionExecutionInfo execution_info_;
};

}  // namespace light_ocr::internal
