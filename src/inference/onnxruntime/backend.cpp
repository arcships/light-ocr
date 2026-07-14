#include "inference/onnxruntime/backend.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "util/checked_math.hpp"

namespace light_ocr::internal {
namespace {

Ort::Env& environment() {
  static Ort::Env value(ORT_LOGGING_LEVEL_ERROR, "light-ocr");
  return value;
}

template <class T>
Result<T> runtime_failure(ErrorCode code, const char* message, std::string detail = {}) {
  return Result<T>::failure(Error{code, message, std::move(detail)});
}

bool supported_dimension(std::int64_t value, std::int64_t expected) {
  return value == -1 || value == expected;
}

void validate_model_contract(Ort::Session& session, ModelKind kind,
                             std::size_t expected_recognition_classes) {
  if (session.GetInputCount() != 1 || session.GetOutputCount() != 1) {
    throw std::runtime_error("Model must have exactly one input and one output");
  }
  const auto input_type = session.GetInputTypeInfo(0);
  const auto output_type = session.GetOutputTypeInfo(0);
  const auto input_info = input_type.GetTensorTypeAndShapeInfo();
  const auto output_info = output_type.GetTensorTypeAndShapeInfo();
  if (input_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
      output_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
    throw std::runtime_error(
        "Model input and output tensors must use float32 (input=" +
        std::to_string(static_cast<int>(input_info.GetElementType())) + ", output=" +
        std::to_string(static_cast<int>(output_info.GetElementType())) + ")");
  }
  const auto input_shape = input_info.GetShape();
  const auto output_shape = output_info.GetShape();
  if (input_shape.size() != 4 || !supported_dimension(input_shape[1], 3)) {
    throw std::runtime_error("Model input must be rank-4 NCHW with three channels");
  }
  if (kind == ModelKind::detection) {
    if (output_shape.size() != 3 && output_shape.size() != 4) {
      throw std::runtime_error("Detection output must be rank 3 or 4");
    }
    if (output_shape.size() == 4 && !supported_dimension(output_shape[1], 1)) {
      throw std::runtime_error("Detection output channel dimension must be one");
    }
  } else {
    if (!supported_dimension(input_shape[2], 48) || output_shape.size() != 3) {
      throw std::runtime_error("Recognition model tensor ranks or height are unsupported");
    }
    if (output_shape[2] > 0 &&
        static_cast<std::size_t>(output_shape[2]) != expected_recognition_classes) {
      throw std::runtime_error("Recognition output class count does not match dictionary");
    }
  }
}

}  // namespace

OnnxSession::OnnxSession(std::unique_ptr<Ort::Session> session, std::string input_name,
                         std::string output_name)
    : session_(std::move(session)),
      input_name_(std::move(input_name)),
      output_name_(std::move(output_name)) {}

Result<std::unique_ptr<OnnxSession>> OnnxSession::create(
    const SharedBytes& model, std::uint32_t intra_op_threads,
    std::uint32_t inter_op_threads, ModelKind kind,
    std::size_t expected_recognition_classes) {
  try {
    if (!model || model->empty()) {
      return runtime_failure<std::unique_ptr<OnnxSession>>(
          ErrorCode::invalid_model_bundle, "ONNX model bytes are empty");
    }
    Ort::SessionOptions options;
    options.SetIntraOpNumThreads(static_cast<int>(intra_op_threads));
    options.SetInterOpNumThreads(static_cast<int>(inter_op_threads));
    options.SetExecutionMode(inter_op_threads > 1 ? ORT_PARALLEL : ORT_SEQUENTIAL);
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    auto session = std::make_unique<Ort::Session>(environment(), model->data(), model->size(), options);
    validate_model_contract(*session, kind, expected_recognition_classes);
    Ort::AllocatorWithDefaultOptions allocator;
    auto input_name = session->GetInputNameAllocated(0, allocator);
    auto output_name = session->GetOutputNameAllocated(0, allocator);
    if (!input_name || !output_name || input_name.get()[0] == '\0' || output_name.get()[0] == '\0') {
      return runtime_failure<std::unique_ptr<OnnxSession>>(
          ErrorCode::unsupported_model, "Model input or output name is empty");
    }
    return Result<std::unique_ptr<OnnxSession>>::success(std::unique_ptr<OnnxSession>(
        new OnnxSession(std::move(session), input_name.get(), output_name.get())));
  } catch (const Ort::Exception& exception) {
    return runtime_failure<std::unique_ptr<OnnxSession>>(
        ErrorCode::runtime_initialization_failed, "ONNX Runtime failed to create a session",
        exception.what());
  } catch (const std::exception& exception) {
    return runtime_failure<std::unique_ptr<OnnxSession>>(
        ErrorCode::unsupported_model, "ONNX model contract validation failed", exception.what());
  } catch (...) {
    return runtime_failure<std::unique_ptr<OnnxSession>>(
        ErrorCode::internal_error, "Unknown ONNX Runtime initialization failure");
  }
}

Result<TensorOutput> OnnxSession::run(const std::vector<float>& values,
                                      const std::vector<std::int64_t>& shape) noexcept {
  try {
    if (!session_ || values.empty() || shape.empty()) {
      return runtime_failure<TensorOutput>(ErrorCode::inference_failed,
                                           "Inference input tensor is empty");
    }
    std::uint64_t expected = 1;
    for (const auto dimension : shape) {
      if (dimension <= 0 ||
          !checked_mul<std::uint64_t>(expected, static_cast<std::uint64_t>(dimension), &expected)) {
        return runtime_failure<TensorOutput>(ErrorCode::inference_failed,
                                             "Inference input shape is invalid");
      }
    }
    if (expected != values.size()) {
      return runtime_failure<TensorOutput>(ErrorCode::inference_failed,
                                           "Inference input size does not match shape");
    }
    auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    auto input = Ort::Value::CreateTensor<float>(memory, const_cast<float*>(values.data()),
                                                 values.size(), shape.data(), shape.size());
    const char* input_names[] = {input_name_.c_str()};
    const char* output_names[] = {output_name_.c_str()};
    Ort::RunOptions run_options;
    auto outputs = session_->Run(run_options, input_names, &input, 1, output_names, 1);
    if (outputs.size() != 1 || !outputs[0].IsTensor()) {
      return runtime_failure<TensorOutput>(ErrorCode::inference_failed,
                                           "Inference did not return one tensor");
    }
    const auto info = outputs[0].GetTensorTypeAndShapeInfo();
    if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
      return runtime_failure<TensorOutput>(ErrorCode::inference_failed,
                                           "Inference output is not float32");
    }
    const auto count = info.GetElementCount();
    const auto* output_data = outputs[0].GetTensorData<float>();
    TensorOutput result;
    result.shape = info.GetShape();
    result.values.assign(output_data, output_data + count);
    return Result<TensorOutput>::success(std::move(result));
  } catch (const Ort::Exception& exception) {
    return runtime_failure<TensorOutput>(ErrorCode::inference_failed,
                                         "ONNX Runtime inference failed", exception.what());
  } catch (const std::exception& exception) {
    return runtime_failure<TensorOutput>(ErrorCode::internal_error,
                                         "Unexpected inference failure", exception.what());
  } catch (...) {
    return runtime_failure<TensorOutput>(ErrorCode::internal_error,
                                         "Unknown inference failure");
  }
}

}  // namespace light_ocr::internal
