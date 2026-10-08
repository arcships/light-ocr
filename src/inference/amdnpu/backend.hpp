#pragma once

#include "inference/backend.hpp"

namespace light_ocr::internal {

// Lightweight deployments use the versioned IREE C ABI. Legacy Ryzen AI
// deployments retain their separate ORT C API table and namespace.
class AmdNpuSession final : public InferenceSession {
 public:
  ~AmdNpuSession() noexcept override;
  static Result<std::unique_ptr<AmdNpuSession>> create(
      const InferenceSessionConfig& config, std::size_t classes,
      std::optional<CreationReason>* reason = nullptr) noexcept;
  Result<TensorOutput> run(const std::vector<float>& values,
                          const std::vector<std::int64_t>& shape) noexcept override;
  const SessionExecutionInfo& execution_info() const noexcept override { return info_; }

 private:
  struct State;
  AmdNpuSession(std::unique_ptr<State> state, SessionExecutionInfo info);
  std::unique_ptr<State> state_;
  SessionExecutionInfo info_;
};

}  // namespace light_ocr::internal
