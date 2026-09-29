#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "inference/backend.hpp"
#include "light_ocr/core.hpp"

namespace light_ocr::internal {

// Recognition widths are rounded up to these buckets so the NPU compiles a
// bounded set of static shapes. The list matches the Apple runtime buckets,
// which the Intel NPU spike qualified without trailing-character loss.
inline const std::vector<std::uint32_t>& openvino_recognition_width_buckets() {
  static const std::vector<std::uint32_t> buckets = {
      320,  384,  480,  544,  576,  608,  704,  736,  832,  960,
      1056, 1184, 1248, 1376, 1600, 1984, 2240, 2560, 2880, 3200};
  return buckets;
}

struct OpenVinoDeviceInfo {
  std::string full_name;
  std::string architecture;
  std::string driver_version;
  std::string compiler_version;
  std::string runtime_version;
};

class OpenVinoSession final : public InferenceSession {
 public:
  ~OpenVinoSession() noexcept override;

  // `probe_shape` is compiled during creation so an unsupported model fails
  // before the backend is selected; every other shape compiles on first use.
  static Result<std::unique_ptr<OpenVinoSession>> create(
      const SharedBytes& model, const InferenceSessionConfig& config,
      ModelKind kind, const std::vector<std::int64_t>& probe_shape,
      std::optional<CreationReason>* creation_reason = nullptr) noexcept;

  Result<TensorOutput> run(const std::vector<float>& values,
                           const std::vector<std::int64_t>& shape) noexcept override;

  const SessionExecutionInfo& execution_info() const noexcept override {
    return execution_info_;
  }

  struct State;

 private:
  OpenVinoSession(std::unique_ptr<State> state, SessionExecutionInfo info);

  std::unique_ptr<State> state_;
  SessionExecutionInfo execution_info_;
};

}  // namespace light_ocr::internal
