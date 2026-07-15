#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "light_ocr/error.hpp"
#include "light_ocr/types.hpp"

namespace light_ocr::internal {

struct InferenceSessionConfig {
  std::uint32_t intra_op_threads = 1;
  std::uint32_t inter_op_threads = 1;
  ExecutionProvider provider = ExecutionProvider::cpu;
  SessionFallback session_fallback = SessionFallback::error;
  CpuPartition cpu_partition = CpuPartition::allow;
  std::optional<std::uint32_t> device_id;
  PerformanceHint performance_hint = PerformanceHint::latency;
  Precision precision = Precision::automatic;
  std::string model_id;
  std::string model_sha256;
  std::string shape_policy;
};

class TensorOutput {
 public:
  TensorOutput(std::shared_ptr<void> storage, const float* data,
               std::vector<std::int64_t> shape, std::size_t size)
      : storage_(std::move(storage)), data_(data), shape_(std::move(shape)), size_(size) {}

  TensorOutput(TensorOutput&&) noexcept = default;
  TensorOutput& operator=(TensorOutput&&) noexcept = default;
  TensorOutput(const TensorOutput&) = delete;
  TensorOutput& operator=(const TensorOutput&) = delete;

  const float* data() const noexcept { return data_; }
  std::size_t size() const noexcept { return size_; }
  const std::vector<std::int64_t>& shape() const noexcept { return shape_; }

 private:
  std::shared_ptr<void> storage_;
  const float* data_ = nullptr;
  std::vector<std::int64_t> shape_;
  std::size_t size_ = 0;
};

class InferenceSession {
 public:
  virtual ~InferenceSession() noexcept = default;

  virtual Result<TensorOutput> run(const std::vector<float>& values,
                                   const std::vector<std::int64_t>& shape) noexcept = 0;
  virtual const SessionExecutionInfo& execution_info() const noexcept = 0;
};

}  // namespace light_ocr::internal
