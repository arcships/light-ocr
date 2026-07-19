#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace light_ocr {

enum class ErrorCode {
  invalid_argument,
  invalid_image,
  unsupported_pixel_format,
  unsupported_capability,
  invalid_model_bundle,
  unsupported_model,
  model_integrity_failed,
  runtime_initialization_failed,
  inference_failed,
  postprocess_failed,
  resource_limit_exceeded,
  invalid_engine,
  internal_error,
};

enum class CreationReason {
  adapter_unavailable,
  model_compute_unsupported,
  device_memory_insufficient,
  driver_version_unsupported,
  package_corrupt,
  artifact_hash_mismatch,
  provider_abi_mismatch,
  internal_assertion_failed,
  unrecoverable_load_failed,
};

enum class CreationAttemptStatus { selected, skipped, fatal };

struct CreationAttempt {
  std::string provider;
  CreationAttemptStatus status = CreationAttemptStatus::fatal;
  std::optional<CreationReason> creation_reason;
  std::optional<ErrorCode> error_code;
};

struct CreationTrace {
  std::string requested_provider;
  std::optional<std::string> policy_id;
  std::optional<std::uint32_t> policy_version;
  std::vector<std::string> ordered_candidates;
  std::vector<CreationAttempt> attempts;
  std::optional<std::string> selected_provider;
};

struct Error {
  Error() = default;
  Error(ErrorCode error_code, std::string error_message,
        std::string error_detail = {},
        std::optional<CreationTrace> trace = std::nullopt)
      : code(error_code),
        message(std::move(error_message)),
        detail(std::move(error_detail)),
        creation_trace(std::move(trace)) {}

  ErrorCode code = ErrorCode::internal_error;
  std::string message;
  std::string detail;
  std::optional<CreationTrace> creation_trace;
};

const char* to_string(ErrorCode code) noexcept;
const char* to_string(CreationReason reason) noexcept;
const char* to_string(CreationAttemptStatus status) noexcept;

template <class T>
class Result {
 public:
  static Result success(T value) { return Result(std::move(value)); }
  static Result failure(Error error) { return Result(std::move(error)); }

  bool ok() const noexcept { return std::holds_alternative<T>(value_); }
  explicit operator bool() const noexcept { return ok(); }

  const T& value() const& {
    if (!ok()) throw std::logic_error("Result::value() called on an error");
    return std::get<T>(value_);
  }

  T&& value() && {
    if (!ok()) throw std::logic_error("Result::value() called on an error");
    return std::get<T>(std::move(value_));
  }

  const Error& error() const& {
    if (ok()) throw std::logic_error("Result::error() called on a value");
    return std::get<Error>(value_);
  }

 private:
  explicit Result(T value) : value_(std::move(value)) {}
  explicit Result(Error error) : value_(std::move(error)) {}

  std::variant<T, Error> value_;
};

}  // namespace light_ocr
