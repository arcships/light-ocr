#include "inference/coreml/backend.hpp"

#import <CoreML/CoreML.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include "util/checked_math.hpp"

namespace light_ocr::internal {
namespace {

namespace fs = std::filesystem;

struct PreparedPackage {
  fs::path compiled_path;
  bool cache_hit = false;
};

class AdvisoryLock {
 public:
  explicit AdvisoryLock(const fs::path& path) {
    descriptor_ = ::open(path.c_str(), O_CREAT | O_RDWR, 0600);
    if (descriptor_ < 0 || ::flock(descriptor_, LOCK_EX) != 0) {
      const auto error = errno;
      if (descriptor_ >= 0) ::close(descriptor_);
      throw std::system_error(error, std::generic_category(),
                              "Cannot lock the Core ML cache");
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

 private:
  int descriptor_ = -1;
};

std::mutex& package_mutex() {
  static std::mutex value;
  return value;
}

template <class T>
Result<T> failure(ErrorCode code, const char* message,
                  std::string detail = {}) {
  return Result<T>::failure(Error{code, message, std::move(detail)});
}

void set_creation_reason(std::optional<CreationReason>* output,
                         CreationReason reason) {
  if (output != nullptr) *output = reason;
}

std::string ns_string(NSString* value) {
  if (value == nil) return {};
  const char* utf8 = value.UTF8String;
  return utf8 == nullptr ? std::string{} : std::string(utf8);
}

NSString* to_ns_string(const std::string& value) {
  return [NSString stringWithUTF8String:value.c_str()];
}

std::string error_detail(NSError* error) {
  if (error == nil) return {};
  auto description = ns_string(error.localizedDescription);
  const auto reason = ns_string(error.localizedFailureReason);
  if (!reason.empty() && reason != description) {
    description += description.empty() ? reason : ": " + reason;
  }
  return description;
}

std::string sysctl_string(const char* name) {
  std::size_t size = 0;
  if (::sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size <= 1) {
    return {};
  }
  std::string value(size, '\0');
  if (::sysctlbyname(name, value.data(), &size, nullptr, 0) != 0) return {};
  while (!value.empty() && value.back() == '\0') value.pop_back();
  return value;
}

fs::path cache_root() {
  @autoreleasepool {
    NSArray<NSURL*>* urls = [[NSFileManager defaultManager]
        URLsForDirectory:NSCachesDirectory
               inDomains:NSUserDomainMask];
    NSURL* base = urls.firstObject;
    if (base == nil) {
      base = [NSURL fileURLWithPath:NSTemporaryDirectory() isDirectory:YES];
    }
    const auto path = ns_string(base.path);
    if (path.empty()) throw std::runtime_error("Core ML cache root is unavailable");
    return fs::path(path) / "com.arcships.light-ocr" / "coreml-v1";
  }
}

std::string read_text(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return {};
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

void write_bytes(const fs::path& path, const std::uint8_t* data,
                 std::size_t size) {
  fs::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) throw std::runtime_error("Cannot create Core ML cache file: " + path.string());
  if (size != 0) {
    stream.write(reinterpret_cast<const char*>(data),
                 static_cast<std::streamsize>(size));
  }
  stream.close();
  if (!stream) throw std::runtime_error("Cannot write Core ML cache file: " + path.string());
}

void write_text(const fs::path& path, const std::string& value) {
  write_bytes(path, reinterpret_cast<const std::uint8_t*>(value.data()),
              value.size());
}

std::string operating_system_identity() {
  @autoreleasepool {
    NSProcessInfo* process = [NSProcessInfo processInfo];
    return ns_string(process.operatingSystemVersionString) + "|" +
           sysctl_string("kern.osversion") + "|" + sysctl_string("hw.model") +
           "|" + sysctl_string("machdep.cpu.brand_string");
  }
}

PreparedPackage prepare_package(const AppleModelPackage& package) {
  std::lock_guard<std::mutex> lock(package_mutex());
  const auto root = cache_root();
  fs::create_directories(root);
  const AdvisoryLock cross_process_lock(
      root / (package.package_sha256 + ".lock"));
  const auto package_cache = root / package.package_sha256;
  const auto source_path = package_cache / "source.mlpackage";
  const auto source_marker = package_cache / "source.sha256";
  const auto compiled_path = package_cache / "compiled.mlmodelc";
  const auto compiled_marker = package_cache / "compiled.identity";
  const auto compilation_identity =
      package.package_sha256 + "\n" + operating_system_identity() + "\n";

  bool source_hit = fs::is_directory(source_path) &&
                    read_text(source_marker) == package.package_sha256 + "\n";
  if (!source_hit) {
    std::error_code ignored;
    fs::remove_all(package_cache, ignored);
    const auto temporary = root /
        (package.package_sha256 + ".tmp." + std::to_string(::getpid()));
    fs::remove_all(temporary, ignored);
    const auto temporary_source = temporary / "source.mlpackage";
    for (const auto& file : package.files) {
      if (!file.bytes || file.path.empty()) {
        throw std::runtime_error("Core ML package contains an empty file");
      }
      write_bytes(temporary_source / fs::path(file.path), file.bytes->data(),
                  file.bytes->size());
    }
    write_text(temporary / "source.sha256", package.package_sha256 + "\n");
    fs::rename(temporary, package_cache);
  }

  if (fs::is_directory(compiled_path) &&
      read_text(compiled_marker) == compilation_identity) {
    return PreparedPackage{compiled_path, true};
  }

  @autoreleasepool {
    NSError* error = nil;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    NSURL* compiled = [MLModel
        compileModelAtURL:[NSURL fileURLWithPath:to_ns_string(source_path.string())
                                      isDirectory:YES]
                   error:&error];
#pragma clang diagnostic pop
    if (compiled == nil) {
      throw std::runtime_error("Core ML model compilation failed: " +
                               error_detail(error));
    }
    std::error_code ignored;
    const auto temporary_compiled = package_cache / "compiled.tmp.mlmodelc";
    fs::remove_all(temporary_compiled, ignored);
    error = nil;
    const BOOL copied = [[NSFileManager defaultManager]
        copyItemAtURL:compiled
                 toURL:[NSURL fileURLWithPath:to_ns_string(temporary_compiled.string())
                                      isDirectory:YES]
                 error:&error];
    if (!copied) {
      throw std::runtime_error("Cannot persist compiled Core ML model: " +
                               error_detail(error));
    }
    fs::remove_all(compiled_path, ignored);
    fs::rename(temporary_compiled, compiled_path);
    write_text(compiled_marker, compilation_identity);
  }
  return PreparedPackage{compiled_path, false};
}

float half_to_float(std::uint16_t value) {
  const std::uint32_t sign =
      static_cast<std::uint32_t>(value & 0x8000u) << 16;
  std::uint32_t mantissa = value & 0x03ffu;
  const std::uint32_t encoded_exponent = (value >> 10) & 0x1fu;
  std::uint32_t bits = 0;
  if (encoded_exponent == 0) {
    if (mantissa == 0) {
      bits = sign;
    } else {
      std::int32_t exponent = 1;
      while ((mantissa & 0x0400u) == 0) {
        mantissa <<= 1;
        --exponent;
      }
      mantissa &= 0x03ffu;
      bits = sign |
             (static_cast<std::uint32_t>(exponent + 127 - 15) << 23) |
             (mantissa << 13);
    }
  } else if (encoded_exponent == 0x1fu) {
    bits = sign | 0x7f800000u | (mantissa << 13);
  } else {
    bits = sign | ((encoded_exponent + 127 - 15) << 23) |
           (mantissa << 13);
  }
  float result = 0;
  static_assert(sizeof(result) == sizeof(bits), "float must be IEEE-754 binary32");
  std::memcpy(&result, &bits, sizeof(result));
  return result;
}

NSArray<NSNumber*>* number_array(
    const std::vector<std::int64_t>& values) {
  auto* result = [NSMutableArray<NSNumber*> arrayWithCapacity:values.size()];
  for (const auto value : values) [result addObject:@(value)];
  return result;
}

std::vector<std::int64_t> row_major_strides(
    const std::vector<std::int64_t>& shape) {
  std::vector<std::int64_t> strides(shape.size(), 1);
  for (std::size_t index = shape.size(); index > 1; --index) {
    if (strides[index - 1] <= 0 || shape[index - 1] <= 0) {
      throw std::runtime_error("Core ML input strides overflow");
    }
    std::uint64_t next = 0;
    if (!checked_mul<std::uint64_t>(
            static_cast<std::uint64_t>(strides[index - 1]),
            static_cast<std::uint64_t>(shape[index - 1]), &next) ||
        next > static_cast<std::uint64_t>(
                   std::numeric_limits<std::int64_t>::max())) {
      throw std::runtime_error("Core ML input strides overflow");
    }
    strides[index - 2] = static_cast<std::int64_t>(next);
  }
  return strides;
}

std::string compute_unit_name(MLComputeUnits units) {
  return units == MLComputeUnitsCPUAndNeuralEngine ? "ane" : "gpu";
}

SessionExecutionInfo make_execution_info(
    const InferenceSessionConfig& config, ModelKind kind,
    const PreparedPackage& prepared) {
  SessionExecutionInfo info;
  info.requested_provider = "apple";
  const bool has_neural_engine = coreml_device_has_neural_engine();
  if (!has_neural_engine) {
    info.actual_provider_chain = {"CoreML(MLCPU,MLGPU)"};
    info.device = "cpu+gpu";
  } else if (kind == ModelKind::detection) {
    info.actual_provider_chain = {
        config.cpu_partition == CpuPartition::forbid
            ? "CoreML(MLGPU)"
            : "CoreML(MLNeuralEngine,MLCPU)"};
    info.device = config.cpu_partition == CpuPartition::forbid ? "gpu" : "ane";
  } else {
    info.actual_provider_chain = config.cpu_partition == CpuPartition::forbid
                                     ? std::vector<std::string>{"CoreML(MLGPU)"}
                                     : std::vector<std::string>{
                                           "CoreML(MLNeuralEngine,MLCPU)",
                                           "CoreML(MLGPU)"};
    info.device = config.cpu_partition == CpuPartition::forbid ? "gpu" : "ane+gpu";
  }
  info.precision = "fp16";
  info.device_family = coreml_device_description();
  info.shape_policy = config.shape_policy;
  info.model_id = config.model_id;
  info.model_sha256 = config.model_sha256;
  info.runtime = "Core ML";
  info.runtime_version = ns_string([NSProcessInfo processInfo].operatingSystemVersionString);
  info.operating_system = info.runtime_version;
  info.provider_version = info.runtime_version;
  info.model_cache_status = prepared.cache_hit ? "compiled_cache_hit" : "compiled_cache_miss";
  info.qualification_id = config.apple_package->qualification_id;
  info.device_validated = coreml_device_is_validated(
      config.apple_package->validated_device_families);
  return info;
}

}  // namespace

class CoreMlSession::Impl {
 public:
  Impl(InferenceSessionConfig config, ModelKind kind, PreparedPackage prepared)
      : config_(std::move(config)),
        kind_(kind),
        compiled_path_(std::move(prepared.compiled_path)),
        models_([NSMutableDictionary dictionary]) {
    NSError* error = nil;
    NSURL* url = [NSURL fileURLWithPath:to_ns_string(compiled_path_.string())
                           isDirectory:YES];
    if (@available(macOS 15.0, *)) {
      asset_ = [MLModelAsset modelAssetWithURL:url error:&error];
    }
    if (asset_ == nil) {
      throw std::runtime_error("Core ML failed to open its compiled model asset: " +
                               error_detail(error));
    }
  }

  Result<TensorOutput> run(const std::vector<float>& values,
                           const std::vector<std::int64_t>& shape) {
    if (shape.size() != 4 || shape[0] != 1 || shape[1] != 3) {
      return failure<TensorOutput>(ErrorCode::inference_failed,
                                   "Core ML input must be rank-4 NCHW batch 1");
    }
    std::uint64_t element_count = 1;
    for (const auto dimension : shape) {
      if (dimension <= 0 ||
          !checked_mul<std::uint64_t>(
              element_count, static_cast<std::uint64_t>(dimension),
              &element_count)) {
        return failure<TensorOutput>(ErrorCode::inference_failed,
                                     "Core ML input shape is invalid");
      }
    }
    if (element_count != values.size()) {
      return failure<TensorOutput>(
          ErrorCode::inference_failed,
          "Core ML input size does not match its shape");
    }
    if (kind_ == ModelKind::detection) {
      if (shape[2] < 32 || shape[2] > 960 || shape[3] < 32 ||
          shape[3] > 960) {
        return failure<TensorOutput>(
            ErrorCode::inference_failed,
            "Core ML detection shape is outside the qualified range");
      }
    } else {
      const auto& package = *config_.apple_package;
      if (shape[2] != 48 || shape[3] < 320 || shape[3] > 3200 ||
          shape[3] % package.recognition_width_multiple != 0) {
        return failure<TensorOutput>(
            ErrorCode::inference_failed,
            "Core ML recognition width is not a qualified multiple of 32");
      }
    }

    @autoreleasepool {
      @try {
        const auto function_name =
            kind_ == ModelKind::detection
                ? std::string("main")
                : "w" + std::string(4 - std::to_string(shape[3]).size(), '0') +
                      std::to_string(shape[3]);
        MLComputeUnits compute_units = MLComputeUnitsCPUAndGPU;
        if (coreml_device_has_neural_engine() &&
            config_.cpu_partition == CpuPartition::allow &&
            (kind_ == ModelKind::detection ||
             shape[3] <= config_.apple_package->recognition_ane_maximum_width)) {
          compute_units = MLComputeUnitsCPUAndNeuralEngine;
        }
        auto model_result = model(function_name, compute_units);
        if (!model_result) return Result<TensorOutput>::failure(model_result.error());
        MLModel* selected_model = model_result.value();

        NSError* error = nil;
        const auto strides = row_major_strides(shape);
        MLMultiArray* input = [[MLMultiArray alloc]
            initWithDataPointer:const_cast<float*>(values.data())
                          shape:number_array(shape)
                       dataType:MLMultiArrayDataTypeFloat32
                        strides:number_array(strides)
                    deallocator:nil
                          error:&error];
        if (input == nil) {
          return failure<TensorOutput>(ErrorCode::inference_failed,
                                       "Core ML rejected the input tensor",
                                       error_detail(error));
        }
        NSString* input_name = to_ns_string(config_.apple_package->input_name);
        MLDictionaryFeatureProvider* features =
            [[MLDictionaryFeatureProvider alloc]
                initWithDictionary:@{input_name : input}
                              error:&error];
        if (features == nil) {
          return failure<TensorOutput>(ErrorCode::inference_failed,
                                       "Core ML rejected the input features",
                                       error_detail(error));
        }
        id<MLFeatureProvider> output =
            [selected_model predictionFromFeatures:features error:&error];
        if (output == nil) {
          return failure<TensorOutput>(ErrorCode::inference_failed,
                                       "Core ML prediction failed",
                                       error_detail(error));
        }
        NSString* output_name = to_ns_string(config_.apple_package->output_name);
        MLMultiArray* array = [output featureValueForName:output_name].multiArrayValue;
        if (array == nil ||
            (array.dataType != MLMultiArrayDataTypeFloat32 &&
             array.dataType != MLMultiArrayDataTypeFloat16) ||
            array.count <= 0 || array.shape.count == 0 ||
            array.shape.count != array.strides.count) {
          std::string detail = "nil=" + std::to_string(array == nil) +
                               ",type=" + std::to_string(array.dataType) +
                               ",count=" + std::to_string(array.count) + ",shape=";
          for (NSNumber* value in array.shape) {
            detail += std::to_string(value.longLongValue) + ",";
          }
          detail += "strides=";
          for (NSNumber* value in array.strides) {
            detail += std::to_string(value.longLongValue) + ",";
          }
          return failure<TensorOutput>(
              ErrorCode::inference_failed,
              "Core ML output is not a supported tensor", detail);
        }
        std::vector<std::uint64_t> output_dimensions;
        std::vector<std::uint64_t> output_strides;
        std::vector<std::int64_t> output_shape;
        output_dimensions.reserve(array.shape.count);
        output_strides.reserve(array.strides.count);
        output_shape.reserve(array.shape.count);
        std::uint64_t physical_elements = 1;
        for (NSUInteger index = 0; index < array.shape.count; ++index) {
          const auto dimension = array.shape[index].longLongValue;
          const auto stride = array.strides[index].longLongValue;
          std::uint64_t extent = 0;
          if (dimension <= 0 || stride < 0 ||
              !checked_mul<std::uint64_t>(
                  static_cast<std::uint64_t>(dimension - 1),
                  static_cast<std::uint64_t>(stride), &extent) ||
              !checked_add<std::uint64_t>(physical_elements, extent,
                                          &physical_elements)) {
            return failure<TensorOutput>(
                ErrorCode::inference_failed,
                "Core ML output shape or strides overflow");
          }
          output_dimensions.push_back(static_cast<std::uint64_t>(dimension));
          output_strides.push_back(static_cast<std::uint64_t>(stride));
          output_shape.push_back(dimension);
        }
        auto storage = std::make_shared<std::vector<float>>(
            static_cast<std::size_t>(array.count));
        __block bool copied = false;
        [array getBytesWithHandler:^(const void* bytes, NSInteger size) {
          const auto element_size =
              array.dataType == MLMultiArrayDataTypeFloat32
                  ? sizeof(float)
                  : sizeof(std::uint16_t);
          std::uint64_t required = 0;
          if (bytes != nullptr && size >= 0 &&
              checked_mul<std::uint64_t>(physical_elements, element_size,
                                         &required) &&
              required <= static_cast<std::uint64_t>(size)) {
            const bool last_dimension_contiguous = output_strides.back() == 1;
            const auto inner = last_dimension_contiguous
                                   ? output_dimensions.back()
                                   : std::uint64_t{1};
            const auto outer = storage->size() / inner;
            for (std::uint64_t outer_index = 0; outer_index < outer;
                 ++outer_index) {
              auto coordinates = outer_index;
              std::uint64_t source_offset = 0;
              const auto prefix = last_dimension_contiguous
                                      ? output_dimensions.size() - 1
                                      : output_dimensions.size();
              for (std::size_t index = prefix; index > 0; --index) {
                const auto dimension = output_dimensions[index - 1];
                source_offset +=
                    (coordinates % dimension) * output_strides[index - 1];
                coordinates /= dimension;
              }
              auto* destination = storage->data() + outer_index * inner;
              if (array.dataType == MLMultiArrayDataTypeFloat32) {
                const auto* source = static_cast<const float*>(bytes) +
                                     source_offset;
                std::copy_n(source, inner, destination);
              } else {
                const auto* source =
                    static_cast<const std::uint16_t*>(bytes) + source_offset;
                std::transform(source, source + inner, destination,
                               half_to_float);
              }
            }
            copied = true;
          }
        }];
        if (!copied) {
          return failure<TensorOutput>(ErrorCode::inference_failed,
                                       "Core ML output storage is truncated");
        }
        const auto* data = storage->data();
        const auto size = storage->size();
        return Result<TensorOutput>::success(TensorOutput(
            std::move(storage), data, std::move(output_shape), size));
      } @catch (NSException* exception) {
        return failure<TensorOutput>(ErrorCode::inference_failed,
                                     "Core ML raised an Objective-C exception",
                                     ns_string(exception.reason));
      }
    }
  }

 private:
  Result<MLModel*> model(const std::string& function_name,
                         MLComputeUnits compute_units) {
    const auto key = function_name + ":" + compute_unit_name(compute_units);
    NSString* ns_key = to_ns_string(key);
    MLModel* cached = models_[ns_key];
    if (cached != nil) {
      touch(key);
      return Result<MLModel*>::success(cached);
    }
    MLModelConfiguration* configuration = [[MLModelConfiguration alloc] init];
    configuration.computeUnits = compute_units;
    if (kind_ == ModelKind::recognition) {
      if (@available(macOS 15.0, *)) {
        configuration.functionName = to_ns_string(function_name);
      } else {
        return failure<MLModel*>(ErrorCode::unsupported_capability,
                                 "Core ML multifunction models require macOS 15");
      }
    }
    __block MLModel* loaded = nil;
    __block NSError* error = nil;
    dispatch_semaphore_t completed = dispatch_semaphore_create(0);
    [MLModel loadModelAsset:asset_
              configuration:configuration
          completionHandler:^(MLModel* model, NSError* model_error) {
            loaded = model;
            error = model_error;
            dispatch_semaphore_signal(completed);
          }];
    dispatch_semaphore_wait(completed, DISPATCH_TIME_FOREVER);
    if (loaded == nil) {
      return failure<MLModel*>(ErrorCode::runtime_initialization_failed,
                               "Core ML failed to load a model function",
                               key + ": " + error_detail(error));
    }
    const auto maximum = std::max<std::uint32_t>(
        1, config_.apple_package->maximum_cached_functions);
    while (lru_.size() >= maximum) {
      NSString* victim = to_ns_string(lru_.front());
      [models_ removeObjectForKey:victim];
      lru_.erase(lru_.begin());
    }
    models_[ns_key] = loaded;
    lru_.push_back(key);
    return Result<MLModel*>::success(loaded);
  }

  void touch(const std::string& key) {
    const auto found = std::find(lru_.begin(), lru_.end(), key);
    if (found != lru_.end()) lru_.erase(found);
    lru_.push_back(key);
  }

  InferenceSessionConfig config_;
  ModelKind kind_;
  fs::path compiled_path_;
  MLModelAsset* asset_;
  NSMutableDictionary<NSString*, MLModel*>* models_;
  std::vector<std::string> lru_;
};

bool coreml_device_available() noexcept {
  @autoreleasepool {
    if (@available(macOS 15.0, *)) {
      return true;
    }
    return false;
  }
}

bool coreml_device_has_neural_engine() noexcept {
#if defined(__arm64__)
  return true;
#else
  return false;
#endif
}

std::string coreml_device_architecture() noexcept {
#if defined(__arm64__)
  return "arm64";
#elif defined(__x86_64__)
  return "x86_64";
#else
  return "unknown";
#endif
}

std::string coreml_device_description() noexcept {
  const auto fallback =
      coreml_device_has_neural_engine() ? "Apple Silicon" : "Intel Mac";
  try {
    auto description = sysctl_string("machdep.cpu.brand_string");
    return description.empty() ? fallback : description;
  } catch (...) {
    return fallback;
  }
}

bool coreml_device_is_validated(
    const std::vector<std::string>& device_families) noexcept {
  try {
    const auto device = coreml_device_description();
    return std::any_of(
        device_families.begin(), device_families.end(),
        [&device](const std::string& family) {
          return !family.empty() && device.compare(0, family.size(), family) == 0 &&
                 (device.size() == family.size() ||
                  device[family.size()] == ' ');
        });
  } catch (...) {
    return false;
  }
}

bool coreml_device_is_allowed(
    const std::string& device_policy,
    const std::vector<std::string>& architectures,
    const std::vector<std::string>& validated_device_families) noexcept {
  if (!coreml_device_available() ||
      std::find(architectures.begin(), architectures.end(),
                coreml_device_architecture()) == architectures.end()) {
    return false;
  }
  if (device_policy == "open-macos") return true;
  return device_policy == "validated-only" &&
         coreml_device_is_validated(validated_device_families);
}

CoreMlSession::CoreMlSession(std::unique_ptr<Impl> impl,
                             SessionExecutionInfo execution_info)
    : impl_(std::move(impl)), execution_info_(std::move(execution_info)) {}

CoreMlSession::~CoreMlSession() noexcept = default;

Result<std::unique_ptr<CoreMlSession>> CoreMlSession::create(
    const InferenceSessionConfig& config, ModelKind kind,
    std::optional<CreationReason>* creation_reason) {
  if (creation_reason != nullptr) creation_reason->reset();
  try {
    if (!coreml_device_available()) {
      set_creation_reason(creation_reason,
                          CreationReason::adapter_unavailable);
      return failure<std::unique_ptr<CoreMlSession>>(
          ErrorCode::unsupported_capability,
          "The Apple provider requires macOS 15 or newer");
    }
    if (config.provider != ExecutionProvider::apple || !config.apple_package ||
        (config.precision != Precision::automatic &&
         config.precision != Precision::fp16) ||
        config.device_id || config.performance_hint != PerformanceHint::latency ||
        config.model_id.empty() || config.model_sha256.size() != 64 ||
        config.shape_policy.empty()) {
      set_creation_reason(creation_reason,
                          CreationReason::internal_assertion_failed);
      return failure<std::unique_ptr<CoreMlSession>>(
          ErrorCode::invalid_argument,
          "Apple Core ML session options are invalid");
    }
    if (!coreml_device_is_allowed(
            config.apple_package->device_policy,
            config.apple_package->architectures,
            config.apple_package->validated_device_families)) {
      set_creation_reason(creation_reason,
                          CreationReason::model_compute_unsupported);
      return failure<std::unique_ptr<CoreMlSession>>(
          ErrorCode::unsupported_capability,
          "The Apple provider device policy does not allow this Mac");
    }
    if (!coreml_device_has_neural_engine() &&
        config.cpu_partition == CpuPartition::forbid) {
      set_creation_reason(creation_reason,
                          CreationReason::model_compute_unsupported);
      return failure<std::unique_ptr<CoreMlSession>>(
          ErrorCode::invalid_argument,
          "Intel Mac requires cpuPartition=allow for Core ML CPU+GPU routing");
    }
    const auto prepared = prepare_package(*config.apple_package);
    auto info = make_execution_info(config, kind, prepared);
    auto runtime_config = config;
    runtime_config.apple_package->files.clear();
    auto impl = std::make_unique<Impl>(
        std::move(runtime_config), kind, prepared);
    return Result<std::unique_ptr<CoreMlSession>>::success(
        std::unique_ptr<CoreMlSession>(
            new CoreMlSession(std::move(impl), std::move(info))));
  } catch (const std::exception& exception) {
    return failure<std::unique_ptr<CoreMlSession>>(
        ErrorCode::runtime_initialization_failed,
        "Core ML failed to prepare its model package", exception.what());
  } catch (...) {
    return failure<std::unique_ptr<CoreMlSession>>(
        ErrorCode::internal_error,
        "Unknown Core ML initialization failure");
  }
}

Result<TensorOutput> CoreMlSession::run(
    const std::vector<float>& values,
    const std::vector<std::int64_t>& shape) noexcept {
  try {
    if (!impl_) {
      return failure<TensorOutput>(ErrorCode::inference_failed,
                                   "Core ML session is closed");
    }
    return impl_->run(values, shape);
  } catch (const std::exception& exception) {
    return failure<TensorOutput>(ErrorCode::inference_failed,
                                 "Unexpected Core ML inference failure",
                                 exception.what());
  } catch (...) {
    return failure<TensorOutput>(ErrorCode::internal_error,
                                 "Unknown Core ML inference failure");
  }
}

}  // namespace light_ocr::internal
