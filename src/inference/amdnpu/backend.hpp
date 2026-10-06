#pragma once

#include "inference/backend.hpp"

namespace light_ocr::internal {

// Ryzen AI owns a separate ORT C API table. No Ort:: wrapper or global ORT
// API pointer may be used for objects allocated by this runtime.
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
