#include "inference/onnxruntime/backend.hpp"

#if defined(LIGHT_OCR_HAS_WEBGPU)
#include <onnxruntime_session_options_config_keys.h>

#include <filesystem>
#include <mutex>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "util/checked_math.hpp"
#include "util/sha256.hpp"

namespace light_ocr::internal {
namespace {

Ort::Env& environment() {
  static Ort::Env value(ORT_LOGGING_LEVEL_ERROR, "light-ocr");
  return value;
}

template <class T>
Result<T> runtime_failure(ErrorCode code, const char* message,
                          std::string detail = {}) {
  return Result<T>::failure(Error{code, message, std::move(detail)});
}

void set_creation_reason(std::optional<CreationReason>* output,
                         CreationReason reason) {
  if (output != nullptr) *output = reason;
}

class ModelContractError final : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

#if defined(LIGHT_OCR_HAS_WEBGPU)
constexpr const char* kWebGpuEpName = "WebGpuExecutionProvider";
constexpr const char* kWebGpuRegistrationName = "light-ocr-webgpu";
#if defined(_WIN32)
constexpr const char* kWebGpuLibraryName = "onnxruntime_providers_webgpu.dll";
#else
constexpr const char* kWebGpuLibraryName = "libonnxruntime_providers_webgpu.so";
#endif

class WebGpuSetupError final : public std::runtime_error {
 public:
  WebGpuSetupError(CreationReason reason, std::string message)
      : std::runtime_error(std::move(message)), reason_(reason) {}

  CreationReason reason() const noexcept { return reason_; }

 private:
  CreationReason reason_;
};

struct WebGpuRegistrationState {
  std::mutex mutex;
  std::filesystem::path library;
  bool registered = false;
};

WebGpuRegistrationState& webgpu_registration_state() {
  static WebGpuRegistrationState state;
  return state;
}

std::mutex& webgpu_runtime_mutex() {
  // The plugin registration API and Dawn runtime are process-global. The
  // Windows plugin can access shared Dawn state while sessions are created,
  // run, and destroyed, so keep those provider operations serialized across
  // independent engine workers.
  static std::mutex mutex;
  return mutex;
}

bool is_webgpu_execution(const SessionExecutionInfo& info) {
  return !info.actual_provider_chain.empty() &&
         info.actual_provider_chain.front() == kWebGpuEpName;
}

struct WebGpuTensorStorage {
  WebGpuTensorStorage(std::unique_lock<std::mutex> runtime_lock,
                      Ort::Value output_value)
      : lock(std::move(runtime_lock)), output(std::move(output_value)) {}

  // Members are destroyed in reverse order, keeping the runtime locked until
  // the provider-owned output value has been released.
  std::unique_lock<std::mutex> lock;
  Ort::Value output;
};

#if !defined(_WIN32)
bool linux_drm_render_node_available() {
  std::error_code error;
  std::filesystem::directory_iterator iterator(
      "/dev/dri", std::filesystem::directory_options::skip_permission_denied,
      error);
  const std::filesystem::directory_iterator end;
  while (!error && iterator != end) {
    const auto filename = iterator->path().filename().string();
    if (filename.rfind("renderD", 0) == 0) {
      const auto status = iterator->symlink_status(error);
      if (!error && std::filesystem::is_character_file(status)) return true;
    }
    iterator.increment(error);
  }
  return false;
}
#endif

std::filesystem::path loaded_onnxruntime_directory() {
#if defined(_WIN32)
  const auto module = GetModuleHandleW(L"onnxruntime.dll");
  if (module == nullptr) return {};
  std::vector<wchar_t> buffer(1024);
  for (;;) {
    const auto size = GetModuleFileNameW(
        module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size == 0) return {};
    if (size + 1 < buffer.size()) {
      return std::filesystem::path(
                 std::wstring(buffer.data(), static_cast<std::size_t>(size)))
          .parent_path();
    }
    if (buffer.size() >= 32768) return {};
    buffer.resize(buffer.size() * 2);
  }
#else
  Dl_info information{};
  if (dladdr(reinterpret_cast<const void*>(&OrtGetApiBase), &information) == 0 ||
      information.dli_fname == nullptr || information.dli_fname[0] == '\0') {
    return {};
  }
  return std::filesystem::path(information.dli_fname).parent_path();
#endif
}

std::filesystem::path webgpu_library_path(
    const std::string& configured, std::uint64_t expected_bytes,
    const std::string& expected_sha256) {
  std::filesystem::path library;
  if (configured.empty()) {
    const auto directory = loaded_onnxruntime_directory();
    if (directory.empty()) {
      throw WebGpuSetupError(
          CreationReason::unrecoverable_load_failed,
          "Cannot locate the loaded ONNX Runtime library for WebGPU plugin discovery");
    }
    library = directory / kWebGpuLibraryName;
  } else {
    library = std::filesystem::u8path(configured);
    if (!library.is_absolute()) {
      throw WebGpuSetupError(
          CreationReason::internal_assertion_failed,
          "The WebGPU provider library path must be absolute");
    }
  }
  std::error_code error;
  const auto status = std::filesystem::symlink_status(library, error);
  if (error || !std::filesystem::is_regular_file(status) ||
      std::filesystem::is_symlink(status)) {
    throw WebGpuSetupError(
        CreationReason::package_corrupt,
        "The WebGPU provider library is missing or is not a regular file");
  }
  if (expected_bytes != 0 || !expected_sha256.empty()) {
    const auto actual_bytes = std::filesystem::file_size(library, error);
    if (error || actual_bytes != expected_bytes ||
        expected_bytes > std::numeric_limits<std::size_t>::max() ||
        expected_bytes > static_cast<std::uint64_t>(
                             std::numeric_limits<std::streamsize>::max())) {
      throw WebGpuSetupError(
          CreationReason::artifact_hash_mismatch,
          "The WebGPU provider library byte count does not match its runtime descriptor");
    }
    std::vector<std::uint8_t> contents(static_cast<std::size_t>(expected_bytes));
    std::ifstream input(library, std::ios::binary);
    if (!input ||
        (expected_bytes != 0 &&
         !input.read(reinterpret_cast<char*>(contents.data()),
                     static_cast<std::streamsize>(contents.size()))) ||
        input.peek() != std::ifstream::traits_type::eof()) {
      throw WebGpuSetupError(
          CreationReason::artifact_hash_mismatch,
          "The WebGPU provider library changed while it was being verified");
    }
    if (sha256_hex(contents.data(), contents.size()) != expected_sha256) {
      throw WebGpuSetupError(
          CreationReason::artifact_hash_mismatch,
          "The WebGPU provider library hash does not match its runtime descriptor");
    }
  }
  return library.lexically_normal();
}

std::vector<Ort::ConstEpDevice> webgpu_devices(
    const InferenceSessionConfig& config) {
#if !defined(_WIN32)
  if (!linux_drm_render_node_available()) {
    throw WebGpuSetupError(
        CreationReason::adapter_unavailable,
        "The Linux WebGPU provider requires an accessible DRM render node");
  }
#endif
  const auto library = webgpu_library_path(
      config.webgpu_provider_library, config.webgpu_provider_bytes,
      config.webgpu_provider_sha256);
  auto& state = webgpu_registration_state();
  std::lock_guard<std::mutex> lock(state.mutex);
  auto& env = environment();
  if (state.registered && state.library != library) {
    throw WebGpuSetupError(
        CreationReason::provider_abi_mismatch,
        "A different WebGPU provider library is already registered in this process");
  }
  if (!state.registered) {
    try {
      env.RegisterExecutionProviderLibrary(kWebGpuRegistrationName,
                                           library.native());
    } catch (const Ort::Exception& exception) {
      throw WebGpuSetupError(
          CreationReason::unrecoverable_load_failed,
          std::string("ONNX Runtime could not register the WebGPU provider library: ") +
              exception.what());
    }
    state.library = library;
    state.registered = true;
  }
  std::vector<Ort::ConstEpDevice> selected;
  for (const auto& device : env.GetEpDevices()) {
    const auto* name = device.EpName();
    if (name != nullptr && std::string(name) == kWebGpuEpName) {
      selected.push_back(device);
    }
  }
  if (selected.empty()) {
    throw WebGpuSetupError(
        CreationReason::adapter_unavailable,
        "The WebGPU provider registered successfully but exposed no compatible GPU adapter");
  }
  return selected;
}

std::string webgpu_device_description(
    const std::vector<Ort::ConstEpDevice>& devices) {
  const auto& ep_device = devices.front();
  const auto hardware = ep_device.Device();
  const auto* hardware_vendor = hardware.Vendor();
  const auto* provider_vendor = ep_device.EpVendor();
  std::string description = "webgpu";
  if (hardware_vendor != nullptr && hardware_vendor[0] != '\0') {
    description += ":";
    description += hardware_vendor;
  } else if (provider_vendor != nullptr && provider_vendor[0] != '\0') {
    description += ":";
    description += provider_vendor;
  }
  description += ":" + std::to_string(hardware.VendorId()) + ":" +
                 std::to_string(hardware.DeviceId());
  if (devices.size() > 1) {
    description += ":" + std::to_string(devices.size()) + "-devices";
  }
  return description;
}
#endif

bool supported_dimension(std::int64_t value, std::int64_t expected) {
  return value == -1 || value == expected;
}

void validate_model_contract(Ort::Session& session, ModelKind kind,
                             std::size_t expected_recognition_classes,
                             Precision precision) {
  if (session.GetInputCount() != 1 || session.GetOutputCount() != 1) {
    throw ModelContractError("Model must have exactly one input and one output");
  }
  const auto input_type = session.GetInputTypeInfo(0);
  const auto output_type = session.GetOutputTypeInfo(0);
  const auto input_info = input_type.GetTensorTypeAndShapeInfo();
  const auto output_info = output_type.GetTensorTypeAndShapeInfo();
  const auto expected_type = precision == Precision::fp16
                                 ? ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16
                                 : ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
  if (input_info.GetElementType() != expected_type ||
      output_info.GetElementType() != expected_type) {
    throw ModelContractError(
        std::string("Model input and output tensors must use ") +
        (precision == Precision::fp16 ? "float16" : "float32") + " (input=" +
        std::to_string(static_cast<int>(input_info.GetElementType())) + ", output=" +
        std::to_string(static_cast<int>(output_info.GetElementType())) + ")");
  }
  const auto input_shape = input_info.GetShape();
  const auto output_shape = output_info.GetShape();
  if (input_shape.size() != 4 || !supported_dimension(input_shape[1], 3)) {
    throw ModelContractError("Model input must be rank-4 NCHW with three channels");
  }
  if (kind == ModelKind::detection) {
    if (output_shape.size() != 3 && output_shape.size() != 4) {
      throw ModelContractError("Detection output must be rank 3 or 4");
    }
    if (output_shape.size() == 4 && !supported_dimension(output_shape[1], 1)) {
      throw ModelContractError("Detection output channel dimension must be one");
    }
  } else {
    if (!supported_dimension(input_shape[2], 48) || output_shape.size() != 3) {
      throw ModelContractError("Recognition model tensor ranks or height are unsupported");
    }
    if (output_shape[2] > 0 &&
        static_cast<std::size_t>(output_shape[2]) != expected_recognition_classes) {
      throw ModelContractError("Recognition output class count does not match dictionary");
    }
  }
}

void validate_session_config(const InferenceSessionConfig& config) {
  if (config.intra_op_threads == 0 || config.inter_op_threads == 0) {
    throw std::invalid_argument("ONNX Runtime thread counts must be positive");
  }
  if (config.provider != ExecutionProvider::cpu &&
      config.provider != ExecutionProvider::webgpu) {
    throw std::invalid_argument("ONNX Runtime provider is unsupported");
  }
  if (config.session_fallback != SessionFallback::error) {
    throw std::invalid_argument("Cross-backend session fallback is unsupported");
  }
  if (config.provider == ExecutionProvider::cpu &&
      config.cpu_partition != CpuPartition::allow) {
    throw std::invalid_argument("CPU sessions require cpuPartition=allow");
  }
  if (config.device_id) {
    throw std::invalid_argument(
        "ONNX Runtime WebGPU deviceId is a context ID, not an adapter ordinal");
  }
  if (config.performance_hint != PerformanceHint::latency) {
    throw std::invalid_argument(
        "ONNX Runtime throughput profiles are not qualified in this release");
  }
  if (config.precision != Precision::automatic &&
      config.precision != Precision::fp32 &&
      (config.provider != ExecutionProvider::webgpu ||
       config.precision != Precision::fp16)) {
    throw std::invalid_argument(
        "ONNX Runtime FP16 sessions require the WebGPU provider");
  }
  if (config.model_id.empty() || config.model_sha256.size() != 64 ||
      config.shape_policy.empty() || config.qualification_id.empty()) {
    throw std::invalid_argument("Inference session identity is incomplete");
  }
}

SessionExecutionInfo make_execution_info(const InferenceSessionConfig& config,
                                         std::string webgpu_device) {
  SessionExecutionInfo info;
  const bool webgpu = config.provider == ExecutionProvider::webgpu;
  info.requested_provider = config.requested_provider_override.empty()
                                ? webgpu ? "webgpu" : "cpu"
                                : config.requested_provider_override;
  if (webgpu) {
    info.actual_provider_chain = {"WebGpuExecutionProvider"};
    if (config.cpu_partition == CpuPartition::allow) {
      info.actual_provider_chain.push_back("CPUExecutionProvider");
    }
    info.device = std::move(webgpu_device);
    info.device_validated = config.webgpu_device_validated;
#if defined(_WIN32)
    info.operating_system = "windows";
#elif defined(__linux__)
    info.operating_system = "linux";
#endif
  } else {
    info.actual_provider_chain = {"CPUExecutionProvider"};
    info.device = "cpu";
    info.device_validated = true;
  }
  info.qualification_id = config.qualification_id;
  info.precision = config.precision == Precision::fp16 ? "fp16" : "fp32";
  info.shape_policy = config.shape_policy;
  info.model_id = config.model_id;
  info.model_sha256 = config.model_sha256;
  info.runtime = "ONNX Runtime";
  info.runtime_version = Ort::GetVersionString();
  info.provider_version = webgpu ? "0.1.0" : info.runtime_version;
  info.model_cache_status = "not_applicable";
  info.session_fallback = config.session_fallback_used;
  info.fallback_reason = config.fallback_reason;
  return info;
}

}  // namespace

void add_webgpu_session_config_entries(Ort::SessionOptions& options) {
#if defined(_WIN32)
  options.AddConfigEntry(
      "ep.webgpuexecutionprovider.dawnBackendType", "D3D12");
#else
  options.AddConfigEntry(
      "ep.webgpuexecutionprovider.dawnBackendType", "Vulkan");
#endif
  options.AddConfigEntry(
      "ep.webgpuexecutionprovider.preferredLayout", "NHWC");
  options.AddConfigEntry(
      "ep.webgpuexecutionprovider.enableGraphCapture", "0");
  options.AddConfigEntry(
      "ep.webgpuexecutionprovider.validationMode", "basic");
  options.AddConfigEntry(
      "ep.webgpuexecutionprovider.powerPreference", "high-performance");
}

#if defined(LIGHT_OCR_HAS_WEBGPU) && \
    defined(LIGHT_OCR_WEBGPU_QUALIFICATION_BUILD)
std::string webgpu_profile_prefix() {
#if defined(_WIN32)
  char* value = nullptr;
  std::size_t length = 0;
  std::string result;
  if (_dupenv_s(&value, &length, "LIGHT_OCR_WEBGPU_PROFILE_PREFIX") == 0 &&
      value != nullptr && length > 1) {
    result.assign(value, length - 1);
  }
  std::free(value);
  return result;
#else
  const auto* value = std::getenv("LIGHT_OCR_WEBGPU_PROFILE_PREFIX");
  return value == nullptr ? std::string{} : std::string{value};
#endif
}

void enable_webgpu_qualification_profile(Ort::SessionOptions& options,
                                         ModelKind kind) {
  const auto configured = webgpu_profile_prefix();
  if (configured.empty()) return;
  auto prefix = std::filesystem::u8path(configured);
  if (!prefix.is_absolute()) {
    throw std::invalid_argument(
        "LIGHT_OCR_WEBGPU_PROFILE_PREFIX must be an absolute path");
  }
  static std::atomic<std::uint64_t> sequence{0};
  prefix += kind == ModelKind::detection ? "-detection" : "-recognition";
  prefix += "-" + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
  options.EnableProfiling(prefix.native().c_str());
}
#endif

OnnxSession::OnnxSession(std::unique_ptr<Ort::Session> session, std::string input_name,
                         std::string output_name, Precision precision,
                         SessionExecutionInfo execution_info)
    : session_(std::move(session)),
      input_name_(std::move(input_name)),
      output_name_(std::move(output_name)),
      precision_(precision),
      execution_info_(std::move(execution_info)) {}

Result<std::unique_ptr<OnnxSession>> OnnxSession::create(
    const SharedBytes& model, const InferenceSessionConfig& config, ModelKind kind,
    std::size_t expected_recognition_classes,
    std::optional<CreationReason>* creation_reason) {
  if (creation_reason != nullptr) creation_reason->reset();
  try {
    if (!model || model->empty()) {
      set_creation_reason(creation_reason, CreationReason::package_corrupt);
      return runtime_failure<std::unique_ptr<OnnxSession>>(
          ErrorCode::invalid_model_bundle, "ONNX model bytes are empty");
    }
    validate_session_config(config);
    Ort::SessionOptions options;
    options.SetIntraOpNumThreads(static_cast<int>(config.intra_op_threads));
    options.SetInterOpNumThreads(static_cast<int>(config.inter_op_threads));
    options.SetExecutionMode(config.inter_op_threads > 1 ? ORT_PARALLEL : ORT_SEQUENTIAL);
    options.SetGraphOptimizationLevel(
        config.precision == Precision::fp16
            ? GraphOptimizationLevel::ORT_ENABLE_EXTENDED
            : GraphOptimizationLevel::ORT_ENABLE_ALL);
    std::string selected_webgpu_device;
#if defined(LIGHT_OCR_HAS_WEBGPU)
    std::unique_lock<std::mutex> webgpu_runtime_lock;
#endif
    if (config.provider == ExecutionProvider::webgpu) {
#if defined(LIGHT_OCR_HAS_WEBGPU)
      webgpu_runtime_lock =
          std::unique_lock<std::mutex>(webgpu_runtime_mutex());
      options.DisableMemPattern();
      if (config.cpu_partition == CpuPartition::forbid) {
        options.AddConfigEntry(kOrtSessionOptionsDisableCPUEPFallback, "1");
      }
      add_webgpu_session_config_entries(options);
#if defined(LIGHT_OCR_WEBGPU_QUALIFICATION_BUILD)
      enable_webgpu_qualification_profile(options, kind);
#endif
      const auto devices = webgpu_devices(config);
      selected_webgpu_device = webgpu_device_description(devices);
      options.AppendExecutionProvider_V2(environment(), devices,
                                         Ort::KeyValuePairs{});
#else
      set_creation_reason(creation_reason,
                          CreationReason::provider_abi_mismatch);
      return runtime_failure<std::unique_ptr<OnnxSession>>(
          ErrorCode::unsupported_capability,
          "The WebGPU provider is unavailable in this build");
#endif
    }
    auto session = std::make_unique<Ort::Session>(environment(), model->data(), model->size(), options);
    validate_model_contract(*session, kind, expected_recognition_classes,
                            config.precision);
    Ort::AllocatorWithDefaultOptions allocator;
    auto input_name = session->GetInputNameAllocated(0, allocator);
    auto output_name = session->GetOutputNameAllocated(0, allocator);
    if (!input_name || !output_name || input_name.get()[0] == '\0' || output_name.get()[0] == '\0') {
      set_creation_reason(creation_reason,
                          CreationReason::model_compute_unsupported);
      return runtime_failure<std::unique_ptr<OnnxSession>>(
          ErrorCode::unsupported_model, "Model input or output name is empty");
    }
    return Result<std::unique_ptr<OnnxSession>>::success(std::unique_ptr<OnnxSession>(
        new OnnxSession(std::move(session), input_name.get(), output_name.get(),
                        config.precision,
                        make_execution_info(config,
                                            std::move(selected_webgpu_device)))));
#if defined(LIGHT_OCR_HAS_WEBGPU)
  } catch (const WebGpuSetupError& exception) {
    set_creation_reason(creation_reason, exception.reason());
    return runtime_failure<std::unique_ptr<OnnxSession>>(
        ErrorCode::runtime_initialization_failed,
        "ONNX Runtime WebGPU provider setup failed", exception.what());
#endif
  } catch (const std::invalid_argument& exception) {
    set_creation_reason(creation_reason,
                        CreationReason::internal_assertion_failed);
    return runtime_failure<std::unique_ptr<OnnxSession>>(
        ErrorCode::invalid_argument, "ONNX Runtime session options are invalid",
        exception.what());
  } catch (const Ort::Exception& exception) {
    return runtime_failure<std::unique_ptr<OnnxSession>>(
        ErrorCode::runtime_initialization_failed, "ONNX Runtime failed to create a session",
        exception.what());
  } catch (const ModelContractError& exception) {
    set_creation_reason(creation_reason,
                        CreationReason::model_compute_unsupported);
    return runtime_failure<std::unique_ptr<OnnxSession>>(
        ErrorCode::unsupported_model, "ONNX model contract validation failed",
        exception.what());
  } catch (const std::bad_alloc&) {
    return runtime_failure<std::unique_ptr<OnnxSession>>(
        ErrorCode::resource_limit_exceeded,
        "Host memory allocation failed during ONNX Runtime initialization");
  } catch (const std::exception& exception) {
    return runtime_failure<std::unique_ptr<OnnxSession>>(
        ErrorCode::internal_error, "Unexpected ONNX Runtime initialization failure",
        exception.what());
  } catch (...) {
    return runtime_failure<std::unique_ptr<OnnxSession>>(
        ErrorCode::internal_error, "Unknown ONNX Runtime initialization failure");
  }
}

OnnxSession::~OnnxSession() noexcept {
#if defined(LIGHT_OCR_HAS_WEBGPU)
  if (session_ && is_webgpu_execution(execution_info_)) {
    const std::lock_guard<std::mutex> lock(webgpu_runtime_mutex());
    session_.reset();
  }
#endif
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
#if defined(LIGHT_OCR_HAS_WEBGPU)
    std::unique_lock<std::mutex> webgpu_runtime_lock;
    if (is_webgpu_execution(execution_info_)) {
      webgpu_runtime_lock =
          std::unique_lock<std::mutex>(webgpu_runtime_mutex());
    }
#endif
    auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::vector<Ort::Float16_t> fp16_values;
    Ort::Value input{nullptr};
    if (precision_ == Precision::fp16) {
      fp16_values.reserve(values.size());
      for (const auto value : values) fp16_values.emplace_back(value);
      input = Ort::Value::CreateTensor<Ort::Float16_t>(
          memory, fp16_values.data(), fp16_values.size(), shape.data(),
          shape.size());
    } else {
      input = Ort::Value::CreateTensor<float>(
          memory, const_cast<float*>(values.data()), values.size(), shape.data(),
          shape.size());
    }
    const char* input_names[] = {input_name_.c_str()};
    const char* output_names[] = {output_name_.c_str()};
    Ort::RunOptions run_options;
    auto outputs = session_->Run(run_options, input_names, &input, 1, output_names, 1);
    if (outputs.size() != 1 || !outputs[0].IsTensor()) {
      return runtime_failure<TensorOutput>(ErrorCode::inference_failed,
                                           "Inference did not return one tensor");
    }
    const auto info = outputs[0].GetTensorTypeAndShapeInfo();
    const auto expected_type = precision_ == Precision::fp16
                                   ? ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16
                                   : ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
    if (info.GetElementType() != expected_type) {
      return runtime_failure<TensorOutput>(ErrorCode::inference_failed,
                                           "Inference output tensor type is unexpected");
    }
    const auto count = info.GetElementCount();
    auto output_shape = info.GetShape();
    if (precision_ == Precision::fp16) {
      const auto* source = outputs[0].GetTensorData<Ort::Float16_t>();
      auto storage = std::make_shared<std::vector<float>>();
      storage->reserve(count);
      for (std::size_t index = 0; index < count; ++index) {
        storage->push_back(source[index].ToFloat());
      }
      const auto* data = storage->data();
      return Result<TensorOutput>::success(TensorOutput(
          std::move(storage), data, std::move(output_shape), count));
    }
#if defined(LIGHT_OCR_HAS_WEBGPU)
    if (webgpu_runtime_lock.owns_lock()) {
      auto storage = std::make_shared<WebGpuTensorStorage>(
          std::move(webgpu_runtime_lock), std::move(outputs[0]));
      const auto* data = storage->output.GetTensorData<float>();
      return Result<TensorOutput>::success(TensorOutput(
          std::move(storage), data, std::move(output_shape), count));
    }
#endif
    auto storage = std::make_shared<Ort::Value>(std::move(outputs[0]));
    const auto* data = storage->GetTensorData<float>();
    return Result<TensorOutput>::success(TensorOutput(
        std::move(storage), data, std::move(output_shape), count));
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
