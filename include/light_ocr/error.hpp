#pragma once

#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

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

struct Error {
  ErrorCode code = ErrorCode::internal_error;
  std::string message;
  std::string detail;
};

const char* to_string(ErrorCode code) noexcept;

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
