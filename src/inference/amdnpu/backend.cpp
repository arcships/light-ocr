#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "inference/amdnpu/backend.hpp"

#include <onnxruntime_c_api.h>
#include <dlfcn.h>
#include <link.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>

#include "inference/openvino/backend.hpp"
#include "util/sha256.hpp"

namespace light_ocr::internal {
namespace {
namespace fs = std::filesystem;
constexpr std::uint32_t kApiVersion = 20;

class SetupError final : public std::runtime_error {
 public:
  SetupError(CreationReason reason, const std::string& message)
      : std::runtime_error(message), reason(reason) {}
  CreationReason reason;
};

std::vector<std::uint8_t> read_artifact(const RuntimeArtifact& artifact) {
  const auto path = fs::u8path(artifact.path);
  std::error_code error;
  const auto status = fs::symlink_status(path, error);
  if (!path.is_absolute() || error || !fs::is_regular_file(status) || fs::is_symlink(status)) {
    throw SetupError(CreationReason::package_corrupt, "AMD NPU artifact is missing or not regular");
  }
  const auto size = fs::file_size(path, error);
  if (error || artifact.bytes == 0 || size != artifact.bytes ||
      size > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()) ||
      size > std::numeric_limits<std::size_t>::max()) {
    throw SetupError(CreationReason::artifact_hash_mismatch, "AMD NPU artifact byte count mismatch");
  }
  std::vector<std::uint8_t> contents(static_cast<std::size_t>(size));
  std::ifstream stream(path, std::ios::binary);
  if (!stream.read(reinterpret_cast<char*>(contents.data()), static_cast<std::streamsize>(size)) ||
      stream.peek() != std::ifstream::traits_type::eof() ||
      sha256_hex(contents.data(), contents.size()) != artifact.sha256) {
    throw SetupError(CreationReason::artifact_hash_mismatch, "AMD NPU artifact hash mismatch");
  }
  return contents;
}

std::string read_word(const fs::path& path) {
  std::ifstream stream(path);
  std::string value;
  stream >> value;
  return value;
}

bool stx_npu_available() {
  std::error_code error;
  fs::directory_iterator it("/sys/class/accel", fs::directory_options::skip_permission_denied, error);
  for (; !error && it != fs::directory_iterator(); it.increment(error)) {
    const auto device = it->path() / "device";
    if (read_word(device / "vendor") == "0x1022" && read_word(device / "device") == "0x17f0") {
      const auto node = fs::path("/dev/accel") / it->path().filename();
      if (::access(node.c_str(), R_OK | W_OK) == 0) return true;
    }
  }
  return false;
}

void check(const OrtApi& api, OrtStatus* status) {
  if (status == nullptr) return;
  const std::string message(api.GetErrorMessage(status));
  api.ReleaseStatus(status);
  throw std::runtime_error(message);
}

template <class T, class Release>
auto owned(T* pointer, Release release) {
  return std::unique_ptr<T, Release>(pointer, release);
}

struct Runtime {
  const OrtApi* api = nullptr;
  OrtEnv* environment = nullptr;
  std::string version;
};

std::shared_ptr<Runtime> runtime_for(const RuntimeArtifact& artifact) {
  // dlmopen prevents Ryzen AI's ORT/provider symbols from binding to the
  // CPU/WebGPU ORT already loaded in the primary namespace. Libraries stay
  // mapped because AMD providers keep process/thread-local state.
  static std::mutex mutex;
  static auto* runtimes = new std::map<std::string, std::shared_ptr<Runtime>>;
  const std::lock_guard<std::mutex> lock(mutex);
  (void)read_artifact(artifact);
  const auto key = artifact.path + ":" + artifact.sha256;
  const auto found = runtimes->find(key);
  if (found != runtimes->end()) return found->second;
  void* handle = ::dlmopen(LM_ID_NEWLM, artifact.path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    const char* error = ::dlerror();
    throw SetupError(CreationReason::unrecoverable_load_failed,
                     error ? error : "Cannot load the Ryzen AI runtime");
  }
  auto get_api = reinterpret_cast<const OrtApiBase* (*)()>(::dlsym(handle, "OrtGetApiBase"));
  if (!get_api || !get_api() || !get_api()->GetApi(kApiVersion)) {
    throw SetupError(CreationReason::provider_abi_mismatch, "Ryzen AI runtime has no ORT C API 20");
  }
  auto runtime = std::make_shared<Runtime>();
  runtime->api = get_api()->GetApi(kApiVersion);
  runtime->version = get_api()->GetVersionString();
  char** providers = nullptr;
  int count = 0;
  check(*runtime->api, runtime->api->GetAvailableProviders(&providers, &count));
  bool available = false;
  for (int i = 0; i < count; ++i) {
    if (std::string(providers[i]) == "VitisAIExecutionProvider") available = true;
  }
  check(*runtime->api, runtime->api->ReleaseAvailableProviders(providers, count));
  if (!available) {
    throw SetupError(CreationReason::provider_abi_mismatch,
                     "The supplied Ryzen AI runtime does not include VitisAIExecutionProvider");
  }
  check(*runtime->api, runtime->api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "light-ocr-amdnpu",
                                             &runtime->environment));
  runtimes->emplace(key, runtime);
  return runtime;
}

std::vector<std::int64_t> tensor_shape(const OrtApi& api, const OrtTensorTypeAndShapeInfo* tensor) {
  if (!tensor) throw std::runtime_error("AMD NPU model input/output must be a tensor");
  std::size_t rank = 0;
  check(api, api.GetDimensionsCount(tensor, &rank));
  if (rank > 8) throw std::runtime_error("AMD NPU tensor rank is outside the contract");
  std::vector<std::int64_t> shape(rank);
  check(api, api.GetDimensions(tensor, shape.data(), shape.size()));
  ONNXTensorElementDataType type = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
  check(api, api.GetTensorElementType(tensor, &type));
  if (type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
    throw std::runtime_error("AMD NPU model must expose FP32 input and output tensors");
  }
  return shape;
}
}  // namespace

struct AmdNpuSession::State {
  struct Session {
    OrtSession* value = nullptr;
    std::string input;
    std::string output;
  };
  std::shared_ptr<Runtime> runtime;
  std::vector<AmdNpuRecognitionModel> models;
  std::string compiler_configuration;
  std::map<std::uint32_t, Session> sessions;
  std::size_t classes = 0;
  ~State() {
    for (auto& entry : sessions) runtime->api->ReleaseSession(entry.second.value);
  }
  Session& session_for(std::uint32_t width) {
    const auto existing = sessions.find(width);
    if (existing != sessions.end()) return existing->second;
    const auto model = std::find_if(models.begin(), models.end(), [width](const auto& item) {
      return item.width == width;
    });
    if (model == models.end()) throw std::runtime_error("AMD NPU width has no compiled context model");
    const auto bytes = read_artifact(model->artifact);
    const auto& api = *runtime->api;
    OrtSessionOptions* raw_options = nullptr;
    check(api, api.CreateSessionOptions(&raw_options));
    auto options = owned(raw_options, api.ReleaseSessionOptions);
    check(api, api.SetIntraOpNumThreads(options.get(), 1));
    check(api, api.SetInterOpNumThreads(options.get(), 1));
    const char* keys[] = {"config_file"};
    const char* values[] = {compiler_configuration.c_str()};
    check(api, api.SessionOptionsAppendExecutionProvider_VitisAI(options.get(), keys, values, 1));
    OrtSession* raw_session = nullptr;
    check(api, api.CreateSessionFromArray(runtime->environment, bytes.data(), bytes.size(),
                                         options.get(), &raw_session));
    auto session = owned(raw_session, api.ReleaseSession);
    std::size_t inputs = 0;
    std::size_t outputs = 0;
    check(api, api.SessionGetInputCount(session.get(), &inputs));
    check(api, api.SessionGetOutputCount(session.get(), &outputs));
    if (inputs != 1 || outputs != 1) throw std::runtime_error("AMD NPU model must have one input and output");
    OrtTypeInfo* raw_input = nullptr;
    check(api, api.SessionGetInputTypeInfo(session.get(), 0, &raw_input));
    auto input_type = owned(raw_input, api.ReleaseTypeInfo);
    const OrtTensorTypeAndShapeInfo* input_tensor = nullptr;
    check(api, api.CastTypeInfoToTensorInfo(input_type.get(), &input_tensor));
    if (tensor_shape(api, input_tensor) != std::vector<std::int64_t>{1, 3, 48, width}) {
      throw std::runtime_error("AMD NPU input shape does not match its width bucket");
    }
    OrtTypeInfo* raw_output = nullptr;
    check(api, api.SessionGetOutputTypeInfo(session.get(), 0, &raw_output));
    auto output_type = owned(raw_output, api.ReleaseTypeInfo);
    const OrtTensorTypeAndShapeInfo* output_tensor = nullptr;
    check(api, api.CastTypeInfoToTensorInfo(output_type.get(), &output_tensor));
    const auto output_shape = tensor_shape(api, output_tensor);
    if (output_shape.size() != 3 || output_shape[0] != 1 || output_shape[2] != static_cast<std::int64_t>(classes)) {
      throw std::runtime_error("AMD NPU output class count does not match the locked dictionary");
    }
    OrtAllocator* allocator = nullptr;
    check(api, api.GetAllocatorWithDefaultOptions(&allocator));
    char* raw_input_name = nullptr;
    check(api, api.SessionGetInputName(session.get(), 0, allocator, &raw_input_name));
    auto input_name = owned(raw_input_name, [allocator](char* name) { allocator->Free(allocator, name); });
    char* raw_output_name = nullptr;
    check(api, api.SessionGetOutputName(session.get(), 0, allocator, &raw_output_name));
    auto output_name = owned(raw_output_name, [allocator](char* name) { allocator->Free(allocator, name); });
    Session result{session.get(), input_name.get(), output_name.get()};
    auto& inserted = sessions.emplace(width, std::move(result)).first->second;
    (void)session.release();
    return inserted;
  }
};

AmdNpuSession::AmdNpuSession(std::unique_ptr<State> state, SessionExecutionInfo info)
    : state_(std::move(state)), info_(std::move(info)) {}
AmdNpuSession::~AmdNpuSession() noexcept = default;

Result<std::unique_ptr<AmdNpuSession>> AmdNpuSession::create(
    const InferenceSessionConfig& config, std::size_t classes,
    std::optional<CreationReason>* reason) noexcept {
  using R = Result<std::unique_ptr<AmdNpuSession>>;
  auto fail = [&](CreationReason why, const std::string& message) {
    if (reason) *reason = why;
    return R::failure(Error{ErrorCode::unsupported_capability, message, "amdnpu"});
  };
  try {
    const auto& widths = openvino_recognition_width_buckets();
    if (config.cpu_partition == CpuPartition::forbid ||
        config.model_sha256 != config.amdnpu_source_model_sha256 ||
        config.amdnpu_recognition_models.size() != widths.size()) {
      return fail(CreationReason::model_compute_unsupported,
                  "AMD NPU requires its locked BF16 recognition model and a CPU detector partition");
    }
    for (std::size_t i = 0; i < widths.size(); ++i) {
      if (config.amdnpu_recognition_models[i].width != widths[i]) {
        return fail(CreationReason::model_compute_unsupported, "AMD NPU recognition buckets are incomplete");
      }
    }
    (void)read_artifact(config.amdnpu_runtime);
    (void)read_artifact(config.amdnpu_compiler_configuration);
    if (!stx_npu_available()) {
      return fail(CreationReason::adapter_unavailable, "No accessible AMD STX/KRK NPU on this host");
    }
    auto state = std::make_unique<State>();
    state->runtime = runtime_for(config.amdnpu_runtime);
    state->models = config.amdnpu_recognition_models;
    state->compiler_configuration = config.amdnpu_compiler_configuration.path;
    state->classes = classes;
    try {
      (void)state->session_for(widths.front());
    } catch (const SetupError&) {
      throw;
    } catch (const std::exception& error) {
      return fail(CreationReason::model_compute_unsupported, error.what());
    }
    SessionExecutionInfo info;
    info.requested_provider = config.requested_provider_override.empty() ? "amdnpu" : config.requested_provider_override;
    info.actual_provider_chain = {"VitisAIExecutionProvider", "CPUExecutionProvider"};
    info.device = "npu:AMD XDNA2";
    info.device_family = "STX/KRK";
    info.operating_system = "linux";
    info.precision = "bf16";
    info.shape_policy = "nchw-static-amd-bf16-buckets-v1";
    info.model_id = config.model_id;
    info.model_sha256 = config.model_sha256;
    info.runtime = "Ryzen AI ONNX Runtime";
    info.runtime_version = state->runtime->version;
    info.model_cache_status = "precompiled";
    info.qualification_id = config.qualification_id;
    info.device_validated = config.npu_device_validated;
    return R::success(std::unique_ptr<AmdNpuSession>(new AmdNpuSession(std::move(state), std::move(info))));
  } catch (const SetupError& error) {
    return fail(error.reason, error.what());
  } catch (const std::exception& error) {
    return fail(CreationReason::unrecoverable_load_failed, error.what());
  }
}

Result<TensorOutput> AmdNpuSession::run(const std::vector<float>& values,
                                      const std::vector<std::int64_t>& shape) noexcept {
  try {
    if (shape.size() != 4 || shape[0] != 1 || shape[1] != 3 || shape[2] != 48 ||
        shape[3] <= 0 || shape[3] > 3200 ||
        values.size() != static_cast<std::size_t>(144 * shape[3])) {
      throw std::runtime_error("AMD NPU input does not match a recognition bucket");
    }
    auto& session = state_->session_for(static_cast<std::uint32_t>(shape[3]));
    const auto& api = *state_->runtime->api;
    OrtMemoryInfo* raw_memory = nullptr;
    check(api, api.CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &raw_memory));
    auto memory = owned(raw_memory, api.ReleaseMemoryInfo);
    OrtValue* raw_input = nullptr;
    check(api, api.CreateTensorWithDataAsOrtValue(memory.get(), const_cast<float*>(values.data()),
        values.size() * sizeof(float), shape.data(), shape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &raw_input));
    auto input = owned(raw_input, api.ReleaseValue);
    const OrtValue* inputs[] = {input.get()};
    const char* input_names[] = {session.input.c_str()};
    const char* output_names[] = {session.output.c_str()};
    OrtValue* raw_output = nullptr;
    check(api, api.Run(session.value, nullptr, input_names, inputs, 1, output_names, 1, &raw_output));
    auto output = owned(raw_output, api.ReleaseValue);
    OrtTensorTypeAndShapeInfo* raw_type = nullptr;
    check(api, api.GetTensorTypeAndShape(output.get(), &raw_type));
    auto type = owned(raw_type, api.ReleaseTensorTypeAndShapeInfo);
    const auto output_shape = tensor_shape(api, type.get());
    if (output_shape.size() != 3 || output_shape[0] != 1 || output_shape[1] <= 0 ||
        output_shape[2] != static_cast<std::int64_t>(state_->classes)) {
      throw std::runtime_error("AMD NPU returned an invalid recognition tensor");
    }
    std::size_t count = 0;
    check(api, api.GetTensorShapeElementCount(type.get(), &count));
    if (count == 0 || count > std::numeric_limits<std::size_t>::max() / sizeof(float)) {
      throw std::runtime_error("AMD NPU output tensor is too large");
    }
    void* data = nullptr;
    check(api, api.GetTensorMutableData(output.get(), &data));
    auto copied = std::make_shared<std::vector<float>>(static_cast<float*>(data), static_cast<float*>(data) + count);
    return Result<TensorOutput>::success(TensorOutput(copied, copied->data(), output_shape, count));
  } catch (const std::exception& error) {
    return Result<TensorOutput>::failure(Error{ErrorCode::inference_failed, "AMD NPU inference failed", error.what()});
  }
}

}  // namespace light_ocr::internal
