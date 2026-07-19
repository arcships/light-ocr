#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <onnxruntime_cxx_api.h>

#include "inference/backend.hpp"
#include "light_ocr/core.hpp"

namespace light_ocr::internal {

void add_webgpu_session_config_entries(Ort::SessionOptions& options);

class OnnxSession final : public InferenceSession {
 public:
  ~OnnxSession() noexcept override;

  static Result<std::unique_ptr<OnnxSession>> create(
      const SharedBytes& model, const InferenceSessionConfig& config, ModelKind kind,
      std::size_t expected_recognition_classes = 0,
      std::optional<CreationReason>* creation_reason = nullptr);

  Result<TensorOutput> run(const std::vector<float>& values,
                           const std::vector<std::int64_t>& shape) noexcept override;

  const SessionExecutionInfo& execution_info() const noexcept override {
    return execution_info_;
  }

  const std::string& input_name() const noexcept { return input_name_; }
  const std::string& output_name() const noexcept { return output_name_; }

 private:
  OnnxSession(std::unique_ptr<Ort::Session> session, std::string input_name,
              std::string output_name, Precision precision,
              SessionExecutionInfo execution_info);

  std::unique_ptr<Ort::Session> session_;
  std::string input_name_;
  std::string output_name_;
  Precision precision_ = Precision::fp32;
  SessionExecutionInfo execution_info_;
};

}  // namespace light_ocr::internal
