#include "inference/openvino/backend.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <openvino/c/openvino.h>

#include "util/sha256.hpp"

#if !defined(LIGHT_OCR_OPENVINO_DEFAULT_LIBRARY)
#error "LIGHT_OCR_OPENVINO_DEFAULT_LIBRARY must name the build-time OpenVINO C runtime"
#endif

namespace light_ocr::internal {
namespace {

namespace fs = std::filesystem;

constexpr const char* kDevice = "NPU";
constexpr std::uint64_t kCacheByteLimit = 256ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaximumDetectionShapes = 32;
constexpr std::size_t kMaximumRecognitionShapes = 20;

class OpenVinoSetupError final : public std::runtime_error {
 public:
  OpenVinoSetupError(CreationReason reason, const std::string& message,
                     std::string detail = {})
      : std::runtime_error(message), reason_(reason), detail_(std::move(detail)) {}

  CreationReason reason() const noexcept { return reason_; }
  const std::string& detail() const noexcept { return detail_; }

 private:
  CreationReason reason_;
  std::string detail_;
};

struct Api {
  decltype(&::ov_core_create) core_create = nullptr;
  decltype(&::ov_core_get_available_devices) core_get_available_devices = nullptr;
  decltype(&::ov_available_devices_free) available_devices_free = nullptr;
  decltype(&::ov_core_get_property) core_get_property = nullptr;
  decltype(&::ov_core_read_model_from_memory_buffer) core_read_model_from_memory_buffer = nullptr;
  decltype(&::ov_core_compile_model) core_compile_model = nullptr;
  decltype(&::ov_model_free) model_free = nullptr;
  decltype(&::ov_model_reshape_single_input) model_reshape_single_input = nullptr;
  decltype(&::ov_partial_shape_create_static) partial_shape_create_static = nullptr;
  decltype(&::ov_partial_shape_free) partial_shape_free = nullptr;
  decltype(&::ov_compiled_model_create_infer_request) compiled_model_create_infer_request = nullptr;
  decltype(&::ov_compiled_model_free) compiled_model_free = nullptr;
  decltype(&::ov_compiled_model_get_property) compiled_model_get_property = nullptr;
  decltype(&::ov_infer_request_set_input_tensor_by_index) infer_request_set_input_tensor_by_index = nullptr;
  decltype(&::ov_infer_request_infer) infer_request_infer = nullptr;
  decltype(&::ov_infer_request_get_output_tensor_by_index) infer_request_get_output_tensor_by_index = nullptr;
  decltype(&::ov_infer_request_free) infer_request_free = nullptr;
  decltype(&::ov_tensor_create_from_host_ptr) tensor_create_from_host_ptr = nullptr;
  decltype(&::ov_tensor_get_shape) tensor_get_shape = nullptr;
  decltype(&::ov_tensor_data) tensor_data = nullptr;
  decltype(&::ov_tensor_free) tensor_free = nullptr;
  decltype(&::ov_shape_create) shape_create = nullptr;
  decltype(&::ov_shape_free) shape_free = nullptr;
  decltype(&::ov_free) free = nullptr;
  decltype(&::ov_get_openvino_version) get_openvino_version = nullptr;
  decltype(&::ov_version_free) version_free = nullptr;
  decltype(&::ov_get_last_err_msg) get_last_err_msg = nullptr;
};

// The runtime is loaded once and never unloaded: OpenVINO plugins keep
// process-wide state that is not safe to tear down while other runtimes live.
struct Runtime {
  fs::path library;
  Api api;
  ov_core_t* core = nullptr;
  OpenVinoDeviceInfo device;
  bool device_available = false;
};

struct RuntimeState {
  std::mutex mutex;
  std::unique_ptr<Runtime> runtime;
};

RuntimeState& runtime_state() {
  static auto* state = new RuntimeState();
  return *state;
}

std::string last_error(const Api& api) {
  const char* message = api.get_last_err_msg != nullptr ? api.get_last_err_msg() : nullptr;
  return message != nullptr ? std::string(message) : std::string();
}

fs::path runtime_library_path(const InferenceSessionConfig& config) {
  if (config.openvino_runtime_library.empty()) {
    std::error_code error;
    auto library = fs::canonical(fs::u8path(LIGHT_OCR_OPENVINO_DEFAULT_LIBRARY), error);
    if (error) {
      throw OpenVinoSetupError(CreationReason::package_corrupt,
                               "The build-time OpenVINO runtime library is missing",
                               LIGHT_OCR_OPENVINO_DEFAULT_LIBRARY);
    }
    return library;
  }
  const auto library = fs::u8path(config.openvino_runtime_library);
  if (!library.is_absolute()) {
    throw OpenVinoSetupError(CreationReason::internal_assertion_failed,
                             "The OpenVINO runtime library path must be absolute");
  }
  std::error_code error;
  const auto status = fs::symlink_status(library, error);
  if (error || !fs::is_regular_file(status) || fs::is_symlink(status)) {
    throw OpenVinoSetupError(CreationReason::package_corrupt,
                             "The OpenVINO runtime library is missing or is not a regular file");
  }
  if (config.openvino_runtime_bytes != 0 || !config.openvino_runtime_sha256.empty()) {
    const auto actual_bytes = fs::file_size(library, error);
    if (error || actual_bytes != config.openvino_runtime_bytes ||
        config.openvino_runtime_bytes >
            static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) {
      throw OpenVinoSetupError(
          CreationReason::artifact_hash_mismatch,
          "The OpenVINO runtime library byte count does not match its runtime descriptor");
    }
    std::vector<std::uint8_t> contents(static_cast<std::size_t>(actual_bytes));
    std::ifstream input(library, std::ios::binary);
    if (!input ||
        !input.read(reinterpret_cast<char*>(contents.data()),
                    static_cast<std::streamsize>(contents.size())) ||
        input.peek() != std::ifstream::traits_type::eof()) {
      throw OpenVinoSetupError(
          CreationReason::artifact_hash_mismatch,
          "The OpenVINO runtime library changed while it was being verified");
    }
    if (sha256_hex(contents.data(), contents.size()) != config.openvino_runtime_sha256) {
      throw OpenVinoSetupError(
          CreationReason::artifact_hash_mismatch,
          "The OpenVINO runtime library hash does not match its runtime descriptor");
    }
  }
  return library.lexically_normal();
}

template <class Function>
void bind(void* handle, const char* name, Function* function) {
  void* symbol = ::dlsym(handle, name);
  if (symbol == nullptr) {
    throw OpenVinoSetupError(CreationReason::provider_abi_mismatch,
                             "The OpenVINO runtime does not export a required C API symbol",
                             name);
  }
  *function = reinterpret_cast<Function>(symbol);
}

Api load_api(void* handle) {
  Api api;
  bind(handle, "ov_core_create", &api.core_create);
  bind(handle, "ov_core_get_available_devices", &api.core_get_available_devices);
  bind(handle, "ov_available_devices_free", &api.available_devices_free);
  bind(handle, "ov_core_get_property", &api.core_get_property);
  bind(handle, "ov_core_read_model_from_memory_buffer", &api.core_read_model_from_memory_buffer);
  bind(handle, "ov_core_compile_model", &api.core_compile_model);
  bind(handle, "ov_model_free", &api.model_free);
  bind(handle, "ov_model_reshape_single_input", &api.model_reshape_single_input);
  bind(handle, "ov_partial_shape_create_static", &api.partial_shape_create_static);
  bind(handle, "ov_partial_shape_free", &api.partial_shape_free);
  bind(handle, "ov_compiled_model_create_infer_request", &api.compiled_model_create_infer_request);
  bind(handle, "ov_compiled_model_free", &api.compiled_model_free);
  bind(handle, "ov_compiled_model_get_property", &api.compiled_model_get_property);
  bind(handle, "ov_infer_request_set_input_tensor_by_index",
       &api.infer_request_set_input_tensor_by_index);
  bind(handle, "ov_infer_request_infer", &api.infer_request_infer);
  bind(handle, "ov_infer_request_get_output_tensor_by_index",
       &api.infer_request_get_output_tensor_by_index);
  bind(handle, "ov_infer_request_free", &api.infer_request_free);
  bind(handle, "ov_tensor_create_from_host_ptr", &api.tensor_create_from_host_ptr);
  bind(handle, "ov_tensor_get_shape", &api.tensor_get_shape);
  bind(handle, "ov_tensor_data", &api.tensor_data);
  bind(handle, "ov_tensor_free", &api.tensor_free);
  bind(handle, "ov_shape_create", &api.shape_create);
  bind(handle, "ov_shape_free", &api.shape_free);
  bind(handle, "ov_free", &api.free);
  bind(handle, "ov_get_openvino_version", &api.get_openvino_version);
  bind(handle, "ov_version_free", &api.version_free);
  bind(handle, "ov_get_last_err_msg", &api.get_last_err_msg);
  return api;
}

std::string device_property(const Runtime& runtime, const char* key) {
  char* value = nullptr;
  if (runtime.api.core_get_property(runtime.core, kDevice, key, &value) != OK ||
      value == nullptr) {
    return {};
  }
  std::string result(value);
  runtime.api.free(value);
  return result;
}

bool npu_listed(const Runtime& runtime) {
  ov_available_devices_t devices{};
  if (runtime.api.core_get_available_devices(runtime.core, &devices) != OK) return false;
  bool found = false;
  for (std::size_t index = 0; index < devices.size; ++index) {
    const std::string name = devices.devices[index] != nullptr ? devices.devices[index] : "";
    if (name == kDevice || name.rfind(std::string(kDevice) + ".", 0) == 0) found = true;
  }
  runtime.api.available_devices_free(&devices);
  return found;
}

// Caller holds runtime_state().mutex.
Runtime& load_runtime(const InferenceSessionConfig& config) {
  auto& state = runtime_state();
  const auto library = runtime_library_path(config);
  if (state.runtime) {
    if (state.runtime->library != library) {
      throw OpenVinoSetupError(
          CreationReason::provider_abi_mismatch,
          "A different OpenVINO runtime is already loaded in this process");
    }
    return *state.runtime;
  }
  void* handle = ::dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (handle == nullptr) {
    const char* message = ::dlerror();
    throw OpenVinoSetupError(CreationReason::unrecoverable_load_failed,
                             "Cannot load the OpenVINO runtime",
                             message != nullptr ? message : "");
  }
  auto runtime = std::make_unique<Runtime>();
  runtime->library = library;
  runtime->api = load_api(handle);
  if (runtime->api.core_create(&runtime->core) != OK || runtime->core == nullptr) {
    throw OpenVinoSetupError(CreationReason::unrecoverable_load_failed,
                             "Cannot create the OpenVINO core", last_error(runtime->api));
  }
  ov_version_t version{};
  if (runtime->api.get_openvino_version(&version) == OK) {
    runtime->device.runtime_version =
        version.buildNumber != nullptr ? version.buildNumber : "";
    runtime->api.version_free(&version);
  }
  runtime->device_available = npu_listed(*runtime);
  if (runtime->device_available) {
    runtime->device.full_name = device_property(*runtime, "FULL_DEVICE_NAME");
    runtime->device.architecture = device_property(*runtime, "DEVICE_ARCHITECTURE");
    runtime->device.driver_version = device_property(*runtime, "NPU_DRIVER_VERSION");
    runtime->device.compiler_version = device_property(*runtime, "NPU_COMPILER_VERSION");
  }
  state.runtime = std::move(runtime);
  return *state.runtime;
}

Runtime& require_npu(const InferenceSessionConfig& config) {
  auto& runtime = load_runtime(config);
  if (!runtime.device_available) {
    throw OpenVinoSetupError(CreationReason::adapter_unavailable,
                             "OpenVINO reports no usable Intel NPU on this host");
  }
  if (!config.openvino_runtime_version_prefix.empty() &&
      runtime.device.runtime_version.rfind(config.openvino_runtime_version_prefix, 0) != 0) {
    throw OpenVinoSetupError(CreationReason::provider_abi_mismatch,
                             "OpenVINO version does not match the verified SDK");
  }
  auto at_least = [](const std::string& actual, const std::string& minimum) {
    if (minimum.empty()) return true;
    std::uint64_t a = 0;
    std::uint64_t b = 0;
    const auto left = std::from_chars(actual.data(), actual.data() + actual.size(), a);
    const auto right = std::from_chars(minimum.data(), minimum.data() + minimum.size(), b);
    return left.ec == std::errc{} && left.ptr == actual.data() + actual.size() &&
           right.ec == std::errc{} && right.ptr == minimum.data() + minimum.size() && a >= b;
  };
  // OpenVINO returns numeric driver/compiler identities, not OS package SemVer.
  if (!at_least(runtime.device.driver_version, config.openvino_minimum_driver_version) ||
      !at_least(runtime.device.compiler_version, config.openvino_minimum_compiler_version)) {
    throw OpenVinoSetupError(CreationReason::driver_version_unsupported,
                             "Intel NPU driver or compiler is below the SDK's qualified floor");
  }
  return runtime;
}

fs::path default_cache_root() {
  if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg != nullptr && xdg[0] == '/') {
    return fs::path(xdg) / "com.arcships.light-ocr";
  }
  if (const char* home = std::getenv("HOME"); home != nullptr && home[0] == '/') {
    return fs::path(home) / ".cache" / "com.arcships.light-ocr";
  }
  return {};
}

class AdvisoryLock {
 public:
  explicit AdvisoryLock(const fs::path& path) {
    descriptor_ = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (descriptor_ >= 0 && ::flock(descriptor_, LOCK_EX) != 0) {
      ::close(descriptor_);
      descriptor_ = -1;
    }
  }

  AdvisoryLock(const AdvisoryLock&) = delete;
  AdvisoryLock& operator=(const AdvisoryLock&) = delete;

  ~AdvisoryLock() noexcept {
    if (descriptor_ >= 0) {
      static_cast<void>(::flock(descriptor_, LOCK_UN));
      static_cast<void>(::close(descriptor_));
    }
  }

  bool locked() const noexcept { return descriptor_ >= 0; }

 private:
  int descriptor_ = -1;
};

// Removes the oldest compiled blobs until the whole OpenVINO cache fits the
// byte limit. Caller holds the cache lock.
void prune_cache(const fs::path& root) noexcept {
  try {
    struct Entry {
      fs::path path;
      fs::file_time_type time;
      std::uint64_t bytes;
    };
    std::vector<Entry> entries;
    std::uint64_t total = 0;
    for (const auto& item : fs::recursive_directory_iterator(root)) {
      if (!item.is_regular_file() || item.path().filename() == ".lock") continue;
      Entry entry{item.path(), item.last_write_time(), item.file_size()};
      total += entry.bytes;
      entries.push_back(std::move(entry));
    }
    if (total <= kCacheByteLimit) return;
    std::sort(entries.begin(), entries.end(),
              [](const Entry& left, const Entry& right) { return left.time < right.time; });
    for (const auto& entry : entries) {
      if (total <= kCacheByteLimit) break;
      std::error_code error;
      if (fs::remove(entry.path, error)) total -= entry.bytes;
    }
  } catch (...) {
    // Pruning is best effort; an oversized cache never fails inference.
  }
}

std::vector<std::int64_t> tensor_shape(const Api& api, const ov_tensor_t* tensor) {
  ov_shape_t shape{};
  if (api.tensor_get_shape(tensor, &shape) != OK) return {};
  std::vector<std::int64_t> result(shape.dims, shape.dims + shape.rank);
  api.shape_free(&shape);
  return result;
}

std::string shape_text(const std::vector<std::int64_t>& shape) {
  std::string text;
  for (const auto dimension : shape) {
    if (!text.empty()) text += "x";
    text += std::to_string(dimension);
  }
  return text;
}

}  // namespace

struct OpenVinoSession::State {
  struct Compiled {
    ov_compiled_model_t* model = nullptr;
    ov_infer_request_t* request = nullptr;
    std::uint64_t last_use = 0;
  };

  Runtime* runtime = nullptr;
  ov_model_t* model = nullptr;
  ModelKind kind = ModelKind::detection;
  fs::path cache_directory;
  fs::path cache_lock;
  std::map<std::vector<std::int64_t>, Compiled> compiled;
  std::size_t maximum_shapes = 0;
  std::uint64_t clock = 0;
  std::string last_cache_status = "disabled";

  ~State() {
    for (auto& entry : compiled) release(entry.second);
    if (model != nullptr) runtime->api.model_free(model);
  }

  void release(Compiled& entry) const {
    if (entry.request != nullptr) runtime->api.infer_request_free(entry.request);
    if (entry.model != nullptr) runtime->api.compiled_model_free(entry.model);
    entry = Compiled{};
  }

  // Returns the request for `shape`, compiling it on first use. Throws
  // std::runtime_error with the OpenVINO message on failure.
  ov_infer_request_t* request_for(const std::vector<std::int64_t>& shape) {
    ++clock;
    auto found = compiled.find(shape);
    if (found != compiled.end()) {
      found->second.last_use = clock;
      return found->second.request;
    }
    if (compiled.size() >= maximum_shapes) {
      auto oldest = std::min_element(
          compiled.begin(), compiled.end(), [](const auto& left, const auto& right) {
            return left.second.last_use < right.second.last_use;
          });
      release(oldest->second);
      compiled.erase(oldest);
    }
    const auto& api = runtime->api;
    ov_partial_shape_t partial{};
    if (api.partial_shape_create_static(static_cast<std::int64_t>(shape.size()),
                                        shape.data(), &partial) != OK) {
      throw std::runtime_error("Cannot describe the OpenVINO input shape: " + last_error(api));
    }
    const auto reshaped = api.model_reshape_single_input(model, partial);
    api.partial_shape_free(&partial);
    if (reshaped != OK) {
      throw std::runtime_error("Cannot reshape the OpenVINO model: " + last_error(api));
    }
    Compiled entry;
    ov_status_e status = GENERAL_ERROR;
    {
      std::unique_ptr<AdvisoryLock> lock;
      if (!cache_directory.empty()) lock = std::make_unique<AdvisoryLock>(cache_lock);
      const bool cached = lock && lock->locked();
      const std::string cache = cached ? cache_directory.string() : std::string();
      if (cached) {
        status = api.core_compile_model(
            runtime->core, model, kDevice, 10, &entry.model,
            "PERFORMANCE_HINT", "LATENCY", "INFERENCE_PRECISION_HINT", "f16",
            "NPU_COMPILER_TYPE", "DRIVER", "CACHE_DIR", cache.c_str(),
            "CACHE_MODE", "OPTIMIZE_SIZE");
        if (status == OK) prune_cache(cache_directory.parent_path());
      } else {
        status = api.core_compile_model(
            runtime->core, model, kDevice, 6, &entry.model,
            "PERFORMANCE_HINT", "LATENCY", "INFERENCE_PRECISION_HINT", "f16",
            "NPU_COMPILER_TYPE", "DRIVER");
      }
      last_cache_status = cached ? "miss" : "disabled";
    }
    if (status != OK || entry.model == nullptr) {
      release(entry);
      throw std::runtime_error("OpenVINO cannot compile shape " + shape_text(shape) +
                               " for the NPU: " + last_error(api));
    }
    char* loaded = nullptr;
    if (last_cache_status != "disabled" &&
        api.compiled_model_get_property(entry.model, "LOADED_FROM_CACHE", &loaded) == OK &&
        loaded != nullptr) {
      last_cache_status = std::string(loaded) == "YES" ? "hit" : "miss";
      api.free(loaded);
    }
    if (api.compiled_model_create_infer_request(entry.model, &entry.request) != OK) {
      const auto message = last_error(api);
      release(entry);
      throw std::runtime_error("Cannot create an OpenVINO infer request: " + message);
    }
    entry.last_use = clock;
    return compiled.emplace(shape, entry).first->second.request;
  }
};

OpenVinoSession::OpenVinoSession(std::unique_ptr<State> state,
                                 SessionExecutionInfo info)
    : state_(std::move(state)), execution_info_(std::move(info)) {}

OpenVinoSession::~OpenVinoSession() noexcept {
  try {
    std::lock_guard<std::mutex> lock(runtime_state().mutex);
    state_.reset();
  } catch (...) {
    // Destructors release native handles only; failures cannot be reported.
  }
}

Result<std::unique_ptr<OpenVinoSession>> OpenVinoSession::create(
    const SharedBytes& model, const InferenceSessionConfig& config, ModelKind kind,
    const std::vector<std::int64_t>& probe_shape,
    std::optional<CreationReason>* creation_reason) noexcept {
  using CreateResult = Result<std::unique_ptr<OpenVinoSession>>;
  auto fail = [&](CreationReason reason, ErrorCode code, std::string message,
                  std::string detail) {
    if (creation_reason != nullptr) *creation_reason = reason;
    return CreateResult::failure(Error{code, std::move(message), std::move(detail)});
  };
  try {
    if (!model || model->empty() || config.model_id.empty() ||
        config.model_sha256.size() != 64 || probe_shape.size() != 4) {
      return fail(CreationReason::internal_assertion_failed, ErrorCode::internal_error,
                  "OpenVINO session configuration is incomplete", {});
    }
    std::lock_guard<std::mutex> lock(runtime_state().mutex);
    auto& runtime = require_npu(config);
    auto state = std::make_unique<State>();
    state->runtime = &runtime;
    state->kind = kind;
    state->maximum_shapes = kind == ModelKind::detection ? kMaximumDetectionShapes
                                                         : kMaximumRecognitionShapes;
    if (runtime.api.core_read_model_from_memory_buffer(
            runtime.core, reinterpret_cast<const char*>(model->data()), model->size(),
            nullptr, &state->model) != OK ||
        state->model == nullptr) {
      return fail(CreationReason::unrecoverable_load_failed,
                  ErrorCode::runtime_initialization_failed,
                  "OpenVINO cannot read the locked ONNX model", last_error(runtime.api));
    }

    const auto root = config.openvino_cache_directory.empty()
                          ? default_cache_root()
                          : fs::u8path(config.openvino_cache_directory);
    if (!root.empty()) {
      // Every identity that changes compiled blobs gets its own directory, so
      // a driver or runtime upgrade never loads a stale blob.
      const std::string identity = config.model_sha256 + "\n" +
                                   runtime.device.runtime_version + "\n" +
                                   runtime.device.architecture + "\n" +
                                   runtime.device.driver_version + "\n" +
                                   runtime.device.compiler_version;
      const auto key = sha256_hex(reinterpret_cast<const std::uint8_t*>(identity.data()),
                                  identity.size())
                           .substr(0, 32);
      const auto base = root / "openvino-v1";
      std::error_code error;
      fs::create_directories(base / key, error);
      if (!error) {
        state->cache_directory = base / key;
        state->cache_lock = base / ".lock";
      }
    }

    try {
      state->request_for(probe_shape);
    } catch (const std::exception& exception) {
      return fail(CreationReason::model_compute_unsupported,
                  ErrorCode::unsupported_capability,
                  "The Intel NPU cannot compile the locked model", exception.what());
    }

    SessionExecutionInfo info;
    info.requested_provider = config.requested_provider_override.empty()
                                  ? "openvino"
                                  : config.requested_provider_override;
    info.actual_provider_chain = {"OpenVINO:NPU"};
    info.device = "npu:" + runtime.device.full_name;
    info.device_family = runtime.device.architecture;
    info.operating_system = "linux";
    info.precision = "fp16";
    info.shape_policy = config.shape_policy;
    info.model_id = config.model_id;
    info.model_sha256 = config.model_sha256;
    info.runtime = "OpenVINO";
    info.runtime_version = runtime.device.runtime_version;
    info.provider_version = runtime.device.driver_version;
    info.device_validated = config.npu_device_validated;
    info.model_cache_status = state->last_cache_status;
    info.qualification_id = config.qualification_id;
    return CreateResult::success(std::unique_ptr<OpenVinoSession>(
        new OpenVinoSession(std::move(state), std::move(info))));
  } catch (const OpenVinoSetupError& error) {
    return fail(error.reason(),
                error.reason() == CreationReason::adapter_unavailable
                    ? ErrorCode::unsupported_capability
                    : ErrorCode::runtime_initialization_failed,
                error.what(), error.detail());
  } catch (const std::exception& exception) {
    return fail(CreationReason::unrecoverable_load_failed,
                ErrorCode::runtime_initialization_failed,
                "Cannot create the OpenVINO session", exception.what());
  }
}

Result<TensorOutput> OpenVinoSession::run(
    const std::vector<float>& values, const std::vector<std::int64_t>& shape) noexcept {
  try {
    std::size_t expected = shape.empty() ? 0 : 1;
    for (const auto dimension : shape) {
      if (dimension <= 0) {
        return Result<TensorOutput>::failure(
            Error{ErrorCode::inference_failed, "OpenVINO input shape is invalid", {}});
      }
      expected *= static_cast<std::size_t>(dimension);
    }
    if (shape.size() != 4 || expected != values.size()) {
      return Result<TensorOutput>::failure(
          Error{ErrorCode::inference_failed,
                "OpenVINO input does not match its shape", {}});
    }
    std::lock_guard<std::mutex> lock(runtime_state().mutex);
    const auto& api = state_->runtime->api;
    ov_infer_request_t* request = state_->request_for(shape);

    ov_shape_t input_shape{};
    if (api.shape_create(static_cast<std::int64_t>(shape.size()), shape.data(),
                         &input_shape) != OK) {
      return Result<TensorOutput>::failure(
          Error{ErrorCode::inference_failed, "Cannot describe the OpenVINO input",
                last_error(api)});
    }
    ov_tensor_t* input = nullptr;
    const auto created = api.tensor_create_from_host_ptr(
        F32, input_shape, const_cast<float*>(values.data()), &input);
    api.shape_free(&input_shape);
    if (created != OK || input == nullptr) {
      return Result<TensorOutput>::failure(
          Error{ErrorCode::inference_failed, "Cannot wrap the OpenVINO input",
                last_error(api)});
    }
    const auto set = api.infer_request_set_input_tensor_by_index(request, 0, input);
    const auto inferred = set == OK ? api.infer_request_infer(request) : set;
    api.tensor_free(input);
    if (inferred != OK) {
      return Result<TensorOutput>::failure(
          Error{ErrorCode::inference_failed, "OpenVINO NPU inference failed",
                last_error(api)});
    }

    ov_tensor_t* output = nullptr;
    if (api.infer_request_get_output_tensor_by_index(request, 0, &output) != OK ||
        output == nullptr) {
      return Result<TensorOutput>::failure(
          Error{ErrorCode::inference_failed, "Cannot read the OpenVINO output",
                last_error(api)});
    }
    auto output_shape = tensor_shape(api, output);
    std::size_t size = output_shape.empty() ? 0 : 1;
    for (const auto dimension : output_shape) size *= static_cast<std::size_t>(dimension);
    void* data = nullptr;
    if (output_shape.empty() || api.tensor_data(output, &data) != OK || data == nullptr) {
      api.tensor_free(output);
      return Result<TensorOutput>::failure(
          Error{ErrorCode::inference_failed, "OpenVINO output is empty", last_error(api)});
    }
    // The request reuses its output buffer on the next call, so results are
    // copied into storage owned by the returned tensor.
    auto storage = std::make_shared<std::vector<float>>(
        static_cast<const float*>(data), static_cast<const float*>(data) + size);
    api.tensor_free(output);
    const float* pointer = storage->data();
    return Result<TensorOutput>::success(
        TensorOutput(std::move(storage), pointer, std::move(output_shape), size));
  } catch (const std::exception& exception) {
    return Result<TensorOutput>::failure(
        Error{ErrorCode::inference_failed, "OpenVINO NPU inference failed",
              exception.what()});
  } catch (...) {
    return Result<TensorOutput>::failure(
        Error{ErrorCode::inference_failed, "OpenVINO NPU inference failed", {}});
  }
}

}  // namespace light_ocr::internal
