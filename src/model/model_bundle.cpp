#include "light_ocr/core.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include "model/bundle_data.hpp"
#include "util/checked_math.hpp"
#include "util/sha256.hpp"

namespace light_ocr {
namespace {

#ifndef LIGHT_OCR_VERSION
#define LIGHT_OCR_VERSION "0.0.0"
#endif

using Json = nlohmann::json;

struct BundleFailure : std::runtime_error {
  BundleFailure(ErrorCode error_code, std::string message, std::string detail = {})
      : std::runtime_error(std::move(message)), code(error_code), detail(std::move(detail)) {}
  ErrorCode code;
  std::string detail;
};

struct RuntimeDefaults {
  DetectionStrategy detection_strategy = DetectionStrategy::upstream_exact;
  std::uint32_t detection_max_side = 4'000;
  std::uint32_t recognition_batch_size = 8;
};

[[noreturn]] void invalid(std::string message, std::string detail = {}) {
  throw BundleFailure(ErrorCode::invalid_model_bundle, std::move(message), std::move(detail));
}

[[noreturn]] void unsupported_model(std::string message, std::string detail = {}) {
  throw BundleFailure(ErrorCode::unsupported_model, std::move(message), std::move(detail));
}

void require(bool condition, const std::string& message, const std::string& detail = {}) {
  if (!condition) invalid(message, detail);
}

bool is_normalized_path(const std::string& path) {
  if (path.empty() || path.size() > 1024 || path.front() == '/' || path.back() == '/' ||
      path.find('\\') != std::string::npos || path.find('\0') != std::string::npos) {
    return false;
  }
  std::size_t begin = 0;
  while (begin < path.size()) {
    const auto end = path.find('/', begin);
    const auto length = (end == std::string::npos ? path.size() : end) - begin;
    const auto segment = path.substr(begin, length);
    if (segment.empty() || segment == "." || segment == "..") return false;
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  return true;
}

bool is_sha256(const std::string& value) {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

struct SemanticVersion {
  std::uint32_t major = 0;
  std::uint32_t minor = 0;
  std::uint32_t patch = 0;
};

SemanticVersion parse_semantic_version(const std::string& value, const std::string& context) {
  SemanticVersion result;
  std::array<std::uint32_t*, 3> parts{&result.major, &result.minor, &result.patch};
  std::size_t begin = 0;
  for (std::size_t index = 0; index < parts.size(); ++index) {
    const auto end = index + 1 == parts.size() ? value.size() : value.find('.', begin);
    require(end != std::string::npos && end > begin, "Semantic version is malformed", context);
    const auto* first = value.data() + begin;
    const auto* last = value.data() + end;
    const auto converted = std::from_chars(first, last, *parts[index]);
    require(converted.ec == std::errc{} && converted.ptr == last,
            "Semantic version is malformed", context);
    begin = end + 1;
  }
  require(begin == value.size() + 1, "Semantic version is malformed", context);
  return result;
}

bool version_less(const SemanticVersion& left, const SemanticVersion& right) {
  if (left.major != right.major) return left.major < right.major;
  if (left.minor != right.minor) return left.minor < right.minor;
  return left.patch < right.patch;
}

const SharedBytes& file_at(const std::unordered_map<std::string, SharedBytes>& files,
                           const std::string& path) {
  const auto it = files.find(path);
  if (it == files.end()) invalid("Required bundle file is missing", path);
  return it->second;
}

Json parse_json_file(const std::unordered_map<std::string, SharedBytes>& files,
                     const std::string& path, std::size_t maximum_bytes) {
  const auto& bytes = file_at(files, path);
  require(bytes->size() <= maximum_bytes, "JSON bundle file exceeds its size limit", path);
  try {
    return Json::parse(bytes->begin(), bytes->end());
  } catch (const Json::exception& exception) {
    invalid("Bundle JSON is malformed", path + ": " + exception.what());
  }
}

template <class T>
T required(const Json& object, const char* key, const std::string& context) {
  try {
    return object.at(key).get<T>();
  } catch (const Json::exception& exception) {
    invalid("Missing or invalid bundle field", context + "." + key + ": " + exception.what());
  }
}

std::uint32_t required_u32(const Json& object, const char* key, const std::string& context,
                           std::uint32_t minimum = 1) {
  const auto value = required<std::uint64_t>(object, key, context);
  require(value >= minimum && value <= std::numeric_limits<std::uint32_t>::max(),
          "Bundle integer is outside its supported range", context + "." + key);
  return static_cast<std::uint32_t>(value);
}

std::uint64_t required_u64(const Json& object, const char* key, const std::string& context,
                           std::uint64_t minimum = 1) {
  const auto value = required<std::uint64_t>(object, key, context);
  require(value >= minimum, "Bundle integer is outside its supported range", context + "." + key);
  return value;
}

float required_finite(const Json& object, const char* key, const std::string& context) {
  const auto value = required<double>(object, key, context);
  require(std::isfinite(value) && value >= -std::numeric_limits<float>::max() &&
              value <= std::numeric_limits<float>::max(),
          "Bundle number is not finite", context + "." + key);
  return static_cast<float>(value);
}

std::array<float, 3> required_float3(const Json& object, const char* key,
                                     const std::string& context) {
  Json values;
  try {
    values = object.at(key);
  } catch (const Json::exception& exception) {
    invalid("Missing bundle array", context + "." + key + ": " + exception.what());
  }
  require(values.is_array() && values.size() == 3, "Bundle array must contain three values",
          context + "." + key);
  std::array<float, 3> result{};
  for (std::size_t index = 0; index < result.size(); ++index) {
    const auto value = values[index].get<double>();
    require(std::isfinite(value) && value >= -std::numeric_limits<float>::max() &&
                value <= std::numeric_limits<float>::max(),
            "Bundle array value is outside the supported finite range", context + "." + key);
    result[index] = static_cast<float>(value);
  }
  return result;
}

void require_string(const Json& object, const char* key, const char* expected,
                    const std::string& context) {
  const auto value = required<std::string>(object, key, context);
  require(value == expected, "Unsupported bundle configuration value",
          context + "." + key + "=" + value);
}

void validate_file_inventory(const Json& manifest,
                             const std::unordered_map<std::string, SharedBytes>& files) {
  Json inventory;
  try {
    inventory = manifest.at("files");
  } catch (const Json::exception& exception) {
    invalid("Manifest file inventory is missing", exception.what());
  }
  require(inventory.is_object(), "Manifest files must be an object");

  std::unordered_set<std::string> listed;
  for (const auto& item : inventory.items()) {
    const auto& path = item.key();
    require(is_normalized_path(path), "Manifest contains an invalid path", path);
    require(path != "manifest.json" && path != "SHA256SUMS",
            "Manifest contains a circular or excluded file entry", path);
    require(listed.insert(path).second, "Manifest contains a duplicate path", path);
    const auto& bytes = file_at(files, path);
    const auto expected_bytes = required<std::uint64_t>(item.value(), "bytes", "files." + path);
    require(expected_bytes == bytes->size(), "Bundle payload byte count does not match manifest", path);
    const auto expected_hash = required<std::string>(item.value(), "sha256", "files." + path);
    require(is_sha256(expected_hash),
            "Manifest SHA-256 must contain 64 lowercase hexadecimal characters", path);
    const auto actual_hash = internal::sha256_hex(bytes->data(), bytes->size());
    if (actual_hash != expected_hash) {
      throw BundleFailure(ErrorCode::model_integrity_failed,
                          "Bundle payload SHA-256 does not match manifest", path);
    }
  }

  for (const auto& file : files) {
    if (file.first == "manifest.json" || file.first == "SHA256SUMS") continue;
    require(listed.count(file.first) == 1, "Bundle contains a payload not listed in manifest",
            file.first);
  }
}

void validate_checksum_inventory(const std::unordered_map<std::string, SharedBytes>& files) {
  const auto checksum_bytes = file_at(files, "SHA256SUMS");
  require(checksum_bytes->size() <= 64 * 1024,
          "SHA256SUMS exceeds its size limit", "SHA256SUMS");
  const std::string contents(checksum_bytes->begin(), checksum_bytes->end());
  require(!contents.empty() && contents.back() == '\n',
          "SHA256SUMS must be a non-empty LF-terminated file", "SHA256SUMS");

  std::unordered_set<std::string> listed;
  std::size_t begin = 0;
  while (begin < contents.size()) {
    const auto end = contents.find('\n', begin);
    require(end != std::string::npos && end > begin,
            "SHA256SUMS contains an empty or unterminated line", "SHA256SUMS");
    const auto line = contents.substr(begin, end - begin);
    require(line.size() > 66 && line[64] == ' ' && line[65] == ' ',
            "SHA256SUMS line is malformed", line);
    const auto expected_hash = line.substr(0, 64);
    const auto path = line.substr(66);
    require(is_sha256(expected_hash),
            "SHA256SUMS hash must contain 64 lowercase hexadecimal characters", path);
    require(is_normalized_path(path) && path != "SHA256SUMS",
            "SHA256SUMS contains an invalid path", path);
    require(listed.insert(path).second, "SHA256SUMS contains a duplicate path", path);
    const auto& bytes = file_at(files, path);
    const auto actual_hash = internal::sha256_hex(bytes->data(), bytes->size());
    if (actual_hash != expected_hash) {
      throw BundleFailure(ErrorCode::model_integrity_failed,
                          "Bundle file SHA-256 does not match SHA256SUMS", path);
    }
    begin = end + 1;
  }

  for (const auto& file : files) {
    if (file.first == "SHA256SUMS") continue;
    require(listed.count(file.first) == 1,
            "SHA256SUMS does not list every bundle file", file.first);
  }
}

std::string package_inventory_sha256(
    const std::unordered_map<std::string, SharedBytes>& files,
    const std::string& root_path) {
  const auto prefix = root_path + "/";
  std::vector<std::string> paths;
  for (const auto& file : files) {
    if (file.first.compare(0, prefix.size(), prefix) == 0) {
      paths.push_back(file.first);
    }
  }
  require(!paths.empty(), "Core ML package contains no files", root_path);
  std::sort(paths.begin(), paths.end());
  std::string inventory;
  for (const auto& path : paths) {
    const auto relative = path.substr(prefix.size());
    require(!relative.empty(), "Core ML package contains an invalid file path", path);
    inventory.append(relative);
    inventory.push_back('\0');
    const auto& bytes = file_at(files, path);
    inventory.append(internal::sha256_hex(bytes->data(), bytes->size()));
    inventory.push_back('\n');
  }
  return internal::sha256_hex(
      reinterpret_cast<const std::uint8_t*>(inventory.data()), inventory.size());
}

internal::AppleModelConfig parse_apple_model(
    const Json& model, const std::string& context,
    const std::unordered_map<std::string, SharedBytes>& files) {
  internal::AppleModelConfig result;
  result.model_id = required<std::string>(model, "modelId", context);
  result.package_path = required<std::string>(model, "packagePath", context);
  result.package_sha256 = required<std::string>(model, "packageSha256", context);
  result.input_name = required<std::string>(model, "inputName", context);
  result.output_name = required<std::string>(model, "outputName", context);
  result.shape_policy = required<std::string>(model, "shapePolicy", context);
  require(!result.model_id.empty() && result.model_id.size() <= 128,
          "Core ML model ID is invalid", context);
  require(is_normalized_path(result.package_path) &&
              result.package_path.size() > std::string(".mlpackage").size() &&
              result.package_path.compare(
                  result.package_path.size() - std::string(".mlpackage").size(),
                  std::string(".mlpackage").size(), ".mlpackage") == 0,
          "Core ML package path is invalid", result.package_path);
  require(is_sha256(result.package_sha256),
          "Core ML package SHA-256 is invalid", result.package_path);
  require(!result.input_name.empty() && !result.output_name.empty() &&
              !result.shape_policy.empty(),
          "Core ML tensor contract is incomplete", context);
  file_at(files, result.package_path + "/Manifest.json");
  file_at(files,
          result.package_path + "/Data/com.apple.CoreML/model.mlmodel");
  file_at(files,
          result.package_path + "/Data/com.apple.CoreML/weights/weight.bin");
  require(package_inventory_sha256(files, result.package_path) ==
              result.package_sha256,
          "Core ML package inventory hash does not match its declaration",
          result.package_path);
  return result;
}

std::optional<internal::AppleProviderConfig> parse_apple_provider(
    const Json& manifest, const std::string& schema_version,
    const std::unordered_map<std::string, SharedBytes>& files) {
  if (!manifest.contains("providers")) return std::nullopt;
  const auto& providers = manifest.at("providers");
  if (!providers.contains("apple")) return std::nullopt;
  require(schema_version == "1.1" || schema_version == "1.2",
          "Apple provider payload requires manifest schema 1.1 or 1.2",
          schema_version);
  const auto& apple = providers.at("apple");
  require_string(apple, "schemaVersion", "1.1", "providers.apple");
  internal::AppleProviderConfig result;
  result.minimum_macos =
      required<std::string>(apple, "minimumMacOS", "providers.apple");
  result.device_policy =
      required<std::string>(apple, "devicePolicy", "providers.apple");
  result.architectures = required<std::vector<std::string>>(
      apple, "architectures", "providers.apple");
  result.validated_device_families = required<std::vector<std::string>>(
      apple, "validatedDeviceFamilies", "providers.apple");
  result.qualification_id =
      required<std::string>(apple, "qualificationId", "providers.apple");
  const std::unordered_set<std::string> supported_device_families = {
      "Apple M1", "Apple M2", "Apple M3", "Apple M4"};
  std::unordered_set<std::string> declared_device_families;
  for (const auto& family : result.validated_device_families) {
    require(supported_device_families.count(family) == 1 &&
                declared_device_families.insert(family).second,
            "Apple provider contains an unsupported or duplicate validated family",
            family);
  }
  require(result.minimum_macos == "15.0" &&
              (result.device_policy == "open-macos" ||
               result.device_policy == "validated-only") &&
              result.architectures ==
                  std::vector<std::string>{"arm64", "x86_64"} &&
              !result.validated_device_families.empty() &&
              result.validated_device_families.size() <= 4 &&
              !result.qualification_id.empty() &&
              result.qualification_id.size() <= 128,
          "Apple provider platform contract is unsupported");
  result.detection = parse_apple_model(
      apple.at("detection"), "providers.apple.detection", files);
  result.recognition = parse_apple_model(
      apple.at("recognition"), "providers.apple.recognition", files);
  const auto& detection = apple.at("detection");
  require(required<std::string>(detection, "preferredComputeUnit",
                                "providers.apple.detection") == "ane" &&
              required<std::string>(detection, "strictComputeUnit",
                                    "providers.apple.detection") == "gpu" &&
              required<std::string>(detection, "intelComputeUnit",
                                    "providers.apple.detection") == "cpu+gpu" &&
              required<std::unordered_map<std::string, std::uint32_t>>(
                  detection, "qualifiedMLCPUOperations",
                  "providers.apple.detection") ==
                  std::unordered_map<std::string, std::uint32_t>{
                      {"ios18.relu", 1}, {"pad", 1}} &&
              result.detection.shape_policy ==
                  "nchw-bounded-range-32-960-v1",
          "Apple detection routing contract is unsupported");
  const auto& recognition = apple.at("recognition");
  result.recognition_width_multiple = required_u32(
      recognition, "widthMultiple", "providers.apple.recognition");
  result.recognition_ane_maximum_width = required_u32(
      recognition, "aneMaximumWidth", "providers.apple.recognition");
  result.recognition_runtime_width_buckets =
      required<std::vector<std::uint32_t>>(
          recognition, "runtimeWidthBuckets", "providers.apple.recognition");
  result.maximum_cached_functions = required_u32(
      recognition, "maximumCachedFunctions", "providers.apple.recognition");
  const std::vector<std::uint32_t> expected_runtime_width_buckets = {
      320,  384,  480,  544,  576,  608,  704,
      736,  832,  960,  1056, 1184, 1248, 1376,
      1600, 1984, 2240, 2560, 2880, 3200};
  require(result.recognition_width_multiple == 32 &&
              result.recognition_ane_maximum_width == 1600 &&
              result.recognition_runtime_width_buckets ==
                  expected_runtime_width_buckets &&
              result.maximum_cached_functions ==
                  expected_runtime_width_buckets.size() &&
              required<std::unordered_map<std::string, std::uint32_t>>(
                  recognition, "qualifiedMLCPUOperations",
                  "providers.apple.recognition") ==
                  std::unordered_map<std::string, std::uint32_t>{
                      {"ios18.cast", 1}, {"ios18.conv", 3},
                      {"ios18.relu", 3}, {"pad", 3}} &&
              required<std::string>(recognition, "functionFormat",
                                    "providers.apple.recognition") == "w%04u" &&
              required<std::string>(recognition, "intelComputeUnit",
                                    "providers.apple.recognition") == "cpu+gpu" &&
              result.recognition.shape_policy ==
                  "nchw-static-width-multiple-32-v1",
          "Apple recognition routing contract is unsupported");
  const auto widths = required<std::vector<std::uint32_t>>(
      recognition, "widths", "providers.apple.recognition");
  std::vector<std::uint32_t> expected_widths;
  for (std::uint32_t width = 320; width <= 3200; width += 32) {
    expected_widths.push_back(width);
  }
  require(widths == expected_widths,
          "Apple recognition function inventory is unsupported");
  return result;
}

internal::WebGpuModelConfig parse_webgpu_model(
    const Json& model, const std::string& context,
    const std::unordered_map<std::string, SharedBytes>& files,
    const std::string& expected_model_id, const std::string& source_model_id,
    const std::string& source_model_sha256) {
  internal::WebGpuModelConfig result;
  result.model_id = required<std::string>(model, "modelId", context);
  result.model_path = required<std::string>(model, "modelPath", context);
  result.model_sha256 = required<std::string>(model, "modelSha256", context);
  result.source_model_id =
      required<std::string>(model, "sourceModelId", context);
  result.source_model_sha256 =
      required<std::string>(model, "sourceModelSha256", context);
  require(result.model_id == expected_model_id &&
              is_normalized_path(result.model_path) &&
              result.model_path.compare(0, 7, "webgpu/") == 0 &&
              is_sha256(result.model_sha256) &&
              result.source_model_id == source_model_id &&
              result.source_model_sha256 == source_model_sha256 &&
              required<std::string>(model, "tensorType", context) == "float16",
          "WebGPU FP16 model identity is unsupported", context);
  const auto& bytes = file_at(files, result.model_path);
  require(internal::sha256_hex(bytes->data(), bytes->size()) ==
              result.model_sha256,
          "WebGPU FP16 model hash does not match its declaration",
          result.model_path);
  return result;
}

std::optional<internal::WebGpuProviderConfig> parse_webgpu_provider(
    const Json& manifest, const std::string& schema_version,
    const std::unordered_map<std::string, SharedBytes>& files,
    const std::string& detection_model_id,
    const std::string& detection_model_sha256,
    const std::string& recognition_model_id,
    const std::string& recognition_model_sha256) {
  if (!manifest.contains("providers")) return std::nullopt;
  const auto& providers = manifest.at("providers");
  if (!providers.contains("webgpu")) return std::nullopt;
  require(schema_version == "1.2",
          "WebGPU provider payload requires manifest schema 1.2",
          schema_version);
  const auto& webgpu = providers.at("webgpu");
  require_string(webgpu, "schemaVersion", "1.0", "providers.webgpu");
  require_string(webgpu, "precision", "fp16", "providers.webgpu");
  internal::WebGpuProviderConfig result;
  result.conversion_id =
      required<std::string>(webgpu, "conversionId", "providers.webgpu");
  result.graph_optimization_level = required<std::string>(
      webgpu, "graphOptimizationLevel", "providers.webgpu");
  result.cpu_partition =
      required<std::string>(webgpu, "cpuPartition", "providers.webgpu");
  result.required_cpu_operators = required<std::vector<std::string>>(
      webgpu, "requiredCpuOperators", "providers.webgpu");
  const auto provenance_path = required<std::string>(
      webgpu, "provenancePath", "providers.webgpu");
  const auto provenance_sha256 = required<std::string>(
      webgpu, "provenanceSha256", "providers.webgpu");
  require(result.conversion_id ==
              "onnxruntime-float16-1.24.4-20260719.1" &&
              result.graph_optimization_level == "extended" &&
              result.cpu_partition == "allow-required" &&
              result.required_cpu_operators ==
                  std::vector<std::string>{"Concat", "Gather", "Slice"} &&
              provenance_path == "webgpu/provenance.json" &&
              is_sha256(provenance_sha256),
          "WebGPU FP16 runtime contract is unsupported");
  const auto& provenance = file_at(files, provenance_path);
  require(internal::sha256_hex(provenance->data(), provenance->size()) ==
              provenance_sha256,
          "WebGPU FP16 provenance hash does not match its declaration",
          provenance_path);
  result.detection = parse_webgpu_model(
      webgpu.at("detection"), "providers.webgpu.detection", files,
      "PP-OCRv6_small_det_onnx_webgpu_fp16", detection_model_id,
      detection_model_sha256);
  result.recognition = parse_webgpu_model(
      webgpu.at("recognition"), "providers.webgpu.recognition", files,
      "PP-OCRv6_small_rec_onnx_webgpu_fp16", recognition_model_id,
      recognition_model_sha256);
  require(result.detection.model_path != result.recognition.model_path,
          "WebGPU provider model paths must be distinct");
  return result;
}

internal::DetectionConfig parse_detection(const Json& root,
                                          const std::string& schema_version) {
  const auto& detection = root.at("detection");
  const auto& input = detection.at("input");
  require_string(input, "colorOrder", "BGR", "detection.input");
  require_string(input, "tensorLayout", "NCHW", "detection.input");
  require_string(input, "tensorType", "float32", "detection.input");

  const auto& resize = schema_version != "1.0"
                           ? root.at("sourceDetectionResize")
                           : detection.at("resize");
  const std::string resize_context = schema_version != "1.0"
                                         ? "sourceDetectionResize"
                                         : "detection.resize";
  require_string(resize, "limitType", "min", resize_context);
  require_string(resize, "scaledDimensionRounding", "truncate_toward_zero", resize_context);
  require_string(resize, "multipleRounding", "half_to_even", resize_context);
  require_string(resize, "maxSideLimitOrder", "before_multiple_rounding", resize_context);
  require_string(resize, "interpolation", "linear", resize_context);

  const auto& normalize = detection.at("normalize");
  const auto& postprocess = detection.at("postprocess");
  require_string(postprocess, "algorithm", "DB", "detection.postprocess");
  require_string(postprocess, "scoreMode", "fast", "detection.postprocess");
  require_string(postprocess, "boxType", "quad", "detection.postprocess");

  internal::DetectionConfig config;
  config.limit_side_len = required_u32(resize, "limitSideLen", resize_context);
  config.limit_type = "min";
  config.max_side_limit = required_u32(resize, "maxSideLimit", resize_context);
  config.dimension_multiple = required_u32(resize, "dimensionMultiple", resize_context);
  config.minimum_dimension = required_u32(resize, "minimumDimension", resize_context);
  config.scale = required_finite(normalize, "scale", "detection.normalize");
  config.mean = required_float3(normalize, "mean", "detection.normalize");
  config.std = required_float3(normalize, "std", "detection.normalize");
  config.threshold = required_finite(postprocess, "threshold", "detection.postprocess");
  config.box_threshold = required_finite(postprocess, "boxThreshold", "detection.postprocess");
  config.unclip_ratio = required_finite(postprocess, "unclipRatio", "detection.postprocess");
  config.max_candidates = required_u32(postprocess, "maxCandidates", "detection.postprocess");
  config.use_dilation = required<bool>(postprocess, "useDilation", "detection.postprocess");
  config.score_mode = "fast";
  config.minimum_box_side = required_u32(postprocess, "minimumBoxSide", "detection.postprocess");
  require(config.scale > 0 && config.unclip_ratio > 0 && config.threshold >= 0 &&
              config.threshold <= 1 && config.box_threshold >= 0 && config.box_threshold <= 1,
          "Detection configuration is outside supported ranges");
  require((config.dimension_multiple % 2) == 0 &&
              config.minimum_dimension <= config.max_side_limit,
          "Detection resize rounding configuration is unsupported");
  for (std::size_t i = 0; i < 3; ++i) {
    require(config.std[i] > 0, "Detection standard deviation must be positive");
  }
  return config;
}

RuntimeDefaults parse_runtime_defaults(
    const Json& root, const std::string& schema_version,
    const internal::DetectionConfig& detection) {
  if (schema_version == "1.0") {
    const auto& batch = root.at("recognition").at("batch");
    return RuntimeDefaults{
        DetectionStrategy::upstream_exact, detection.max_side_limit,
        required_u32(batch, "defaultSize", "recognition.batch")};
  }

  const auto& defaults = root.at("runtimeDefaults");
  const auto& default_detection = defaults.at("detection");
  const auto strategy =
      required<std::string>(default_detection, "strategy", "runtimeDefaults.detection");
  RuntimeDefaults result;
  if (strategy == "bounded") {
    result.detection_strategy = DetectionStrategy::bounded;
    require_string(default_detection, "dimensionMultipleRounding", "ceil",
                   "runtimeDefaults.detection");
    require(required_u32(default_detection, "minimumShortSide",
                         "runtimeDefaults.detection") ==
                detection.limit_side_len,
            "Runtime minimum short side must match source detection provenance");
  } else if (strategy == "upstream_exact") {
    result.detection_strategy = DetectionStrategy::upstream_exact;
  } else {
    invalid("Unsupported runtime detection strategy",
            "runtimeDefaults.detection.strategy=" + strategy);
  }
  result.detection_max_side = required_u32(
      default_detection, "maxSide", "runtimeDefaults.detection");
  result.recognition_batch_size = required_u32(
      defaults, "recognitionBatchSize", "runtimeDefaults");
  require(result.detection_max_side <= detection.max_side_limit &&
              result.detection_max_side >= detection.minimum_dimension &&
              (result.detection_strategy != DetectionStrategy::bounded ||
               result.detection_max_side % detection.dimension_multiple == 0) &&
              (result.detection_strategy != DetectionStrategy::upstream_exact ||
               result.detection_max_side == detection.max_side_limit),
          "Runtime detection default is outside source resize limits");
  return result;
}

std::optional<internal::TiledDetectionConfig> parse_tiled_detection(
    const Json& root, const std::string& schema_version,
    const internal::DetectionConfig& detection) {
  if (schema_version != "1.2") return std::nullopt;

  const auto& profile = root.at("runtimeProfiles").at("tiled");
  internal::TiledDetectionConfig result;
  result.contract_version =
      required<std::string>(profile, "contractVersion", "runtimeProfiles.tiled");
  result.tile_side = required_u32(profile, "tileSide", "runtimeProfiles.tiled");
  result.minimum_overlap =
      required_u32(profile, "minimumOverlap", "runtimeProfiles.tiled");
  result.dimension_multiple =
      required_u32(profile, "dimensionMultiple", "runtimeProfiles.tiled");
  result.artificial_boundary_margin = required_u32(
      profile, "artificialBoundaryMargin", "runtimeProfiles.tiled");
  require_string(profile, "dimensionMultipleRounding", "ceil_resize",
                 "runtimeProfiles.tiled");
  require_string(profile, "tileOrder", "row_major", "runtimeProfiles.tiled");
  require_string(profile, "recognition", "once_after_global_merge",
                 "runtimeProfiles.tiled");

  const auto& merge = profile.at("merge");
  result.merge_iou_threshold =
      required_finite(merge, "iouThreshold", "runtimeProfiles.tiled.merge");
  result.merge_ios_threshold = required_finite(
      merge, "intersectionOverSmallerThreshold", "runtimeProfiles.tiled.merge");
  require_string(merge, "scope", "different_overlapping_tiles",
                 "runtimeProfiles.tiled.merge");
  require_string(merge, "geometry", "select_representative",
                 "runtimeProfiles.tiled.merge");
  const auto selection = required<std::vector<std::string>>(
      merge, "selectionOrder", "runtimeProfiles.tiled.merge");
  require(selection == std::vector<std::string>(
                           {"not_artificial_boundary", "higher_db_score",
                            "farther_from_artificial_boundary",
                            "lower_tile_ordinal", "lower_candidate_ordinal"}),
          "Unsupported tiled representative selection order");

  require(result.contract_version == "tiled-v1" && result.tile_side == 1280 &&
              result.minimum_overlap == 128 &&
              result.dimension_multiple == 32 &&
              result.dimension_multiple == detection.dimension_multiple &&
              result.artificial_boundary_margin == 32 &&
              result.artificial_boundary_margin <= result.minimum_overlap &&
              result.tile_side % result.dimension_multiple == 0 &&
              std::abs(result.merge_iou_threshold - 0.5) <= 1e-6 &&
              std::abs(result.merge_ios_threshold - 0.8) <= 1e-6,
          "Unsupported tiled runtime contract", result.contract_version);
  return result;
}

internal::GeometryConfig parse_geometry(const Json& root) {
  const auto& geometry = root.at("geometry");
  require_string(geometry, "perspectiveInterpolation", "cubic", "geometry");
  require_string(geometry, "borderMode", "replicate", "geometry");
  require_string(geometry, "tallLineRotation", "counterclockwise90", "geometry");
  internal::GeometryConfig config;
  config.row_band_pixels = required_u32(geometry, "rowBandPixels", "geometry");
  config.tall_line_ratio = required_finite(geometry, "tallLineRatio", "geometry");
  require(config.tall_line_ratio > 0, "Tall-line ratio must be positive");
  return config;
}

internal::RecognitionConfig parse_recognition(
    const Json& root, const std::unordered_map<std::string, SharedBytes>& files,
    const std::string& manifest_dictionary_path,
    const RuntimeDefaults& runtime_defaults) {
  const auto& recognition = root.at("recognition");
  const auto& input = recognition.at("input");
  require_string(input, "colorOrder", "BGR", "recognition.input");
  require_string(input, "tensorLayout", "NCHW", "recognition.input");
  require_string(input, "tensorType", "float32", "recognition.input");
  require_string(input, "tensorWidthRounding", "truncate_toward_zero", "recognition.input");
  require_string(input, "resizedContentWidthRounding", "ceil", "recognition.input");
  require_string(input, "batchTensorWidth", "maximum_sample_tensor_width", "recognition.input");
  require_string(input, "interpolation", "linear", "recognition.input");

  const auto shape = required<std::vector<std::uint32_t>>(input, "shape", "recognition.input");
  require(shape.size() == 3 && shape[0] == 3 && shape[1] == 48 && shape[2] > 0,
          "Recognition input shape must be [3, 48, baseWidth]");
  const auto& normalize = recognition.at("normalize");
  const auto& batch = recognition.at("batch");
  require(required<bool>(batch, "sortByWidth", "recognition.batch"),
          "Recognition width sorting must be enabled");
  const auto& decode = recognition.at("decode");
  require_string(decode, "algorithm", "CTC", "recognition.decode");
  require_string(decode, "confidence", "mean_selected_argmax_probability", "recognition.decode");
  require(required<bool>(decode, "appendSpaceCharacter", "recognition.decode"),
          "The initial recognition dictionary requires an appended space");

  internal::RecognitionConfig config;
  config.channels = shape[0];
  config.height = shape[1];
  config.base_width = shape[2];
  config.minimum_tensor_width = required_u32(input, "minimumTensorWidth", "recognition.input");
  config.maximum_tensor_width = required_u32(input, "maximumTensorWidth", "recognition.input");
  config.scale = required_finite(normalize, "scale", "recognition.normalize");
  config.mean = required_float3(normalize, "mean", "recognition.normalize");
  config.std = required_float3(normalize, "std", "recognition.normalize");
  config.padding_value = required_finite(normalize, "paddingValue", "recognition.normalize");
  config.default_batch_size = runtime_defaults.recognition_batch_size;
  config.maximum_batch_size = required_u32(batch, "maximumSize", "recognition.batch");
  config.blank_index = required_u32(decode, "blankIndex", "recognition.decode", 0);
  config.collapse_repeats = required<bool>(decode, "collapseRepeats", "recognition.decode");
  config.default_score_threshold =
      required_finite(recognition, "defaultScoreThreshold", "recognition");

  const auto dictionary_path = required<std::string>(decode, "dictionaryPath", "recognition.decode");
  require(is_normalized_path(dictionary_path), "Recognition dictionary path is invalid", dictionary_path);
  require(dictionary_path == manifest_dictionary_path,
          "Recognition dictionary path does not match manifest", dictionary_path);
  const auto dictionary_json = parse_json_file(files, dictionary_path, 4 * 1024 * 1024);
  require(required<std::string>(dictionary_json, "schemaVersion", "recognition.dictionary") ==
              "1.0",
          "Unsupported recognition dictionary schema", dictionary_path);
  try {
    config.characters = dictionary_json.at("characters").get<std::vector<std::string>>();
  } catch (const Json::exception& exception) {
    invalid("Recognition dictionary JSON is invalid", exception.what());
  }
  const auto expected_entries = required_u32(decode, "dictionaryEntries", "recognition.decode");
  require(config.characters.size() == expected_entries,
          "Recognition dictionary entry count does not match configuration", dictionary_path);
  require(!config.characters.empty() && config.characters.back() == " ",
          "Recognition dictionary must end with exactly one ASCII space", dictionary_path);
  std::unordered_set<std::string> dictionary_entries;
  dictionary_entries.reserve(config.characters.size());
  for (const auto& character : config.characters) {
    require(!character.empty(), "Recognition dictionary contains an empty entry", dictionary_path);
    require(dictionary_entries.insert(character).second,
            "Recognition dictionary contains a duplicate entry", dictionary_path);
  }
  if (config.characters.size() > 1) {
    require(config.characters[config.characters.size() - 2] != " ",
            "Recognition dictionary contains a duplicated appended space", dictionary_path);
  }
  require(config.minimum_tensor_width == config.base_width &&
              config.maximum_tensor_width >= config.minimum_tensor_width &&
              config.default_batch_size <= config.maximum_batch_size && config.scale > 0 &&
              config.default_score_threshold >= 0 && config.default_score_threshold <= 1 &&
              config.blank_index == 0 && config.collapse_repeats,
          "Recognition configuration is outside supported ranges");
  for (std::size_t i = 0; i < 3; ++i) {
    require(config.std[i] > 0, "Recognition standard deviation must be positive");
  }
  return config;
}

ResourceLimits parse_limits(const Json& root, const std::string& schema_version) {
  const auto& limits = root.at("resourceLimits");
  ResourceLimits result;
  result.max_width = required_u32(limits, "maxWidth", "resourceLimits");
  result.max_height = required_u32(limits, "maxHeight", "resourceLimits");
  result.max_pixels = required_u64(limits, "maxPixels", "resourceLimits");
  result.max_detection_side = required_u32(limits, "maxDetectionSide", "resourceLimits");
  result.max_detection_candidates =
      required_u32(limits, "maxDetectionCandidates", "resourceLimits");
  result.max_detection_tiles =
      schema_version == "1.2"
          ? required_u32(limits, "maxDetectionTiles", "resourceLimits")
          : ResourceLimits{}.max_detection_tiles;
  result.max_recognition_batch_size =
      required_u32(limits, "maxRecognitionBatchSize", "resourceLimits");
  result.max_recognition_width =
      required_u32(limits, "maxRecognitionWidth", "resourceLimits");
  result.max_temporary_bytes = required_u64(limits, "maxTemporaryBytes", "resourceLimits");
  result.max_concurrent_calls = required_u32(limits, "maxConcurrentCalls", "resourceLimits");
  const ResourceLimits supported;
  require(result.max_width <= supported.max_width &&
              result.max_height <= supported.max_height &&
              result.max_pixels <= supported.max_pixels &&
              result.max_detection_side <= supported.max_detection_side &&
              result.max_detection_candidates <= supported.max_detection_candidates &&
              result.max_detection_tiles <= supported.max_detection_tiles &&
              result.max_recognition_batch_size <= supported.max_recognition_batch_size &&
              result.max_recognition_width <= supported.max_recognition_width &&
              result.max_temporary_bytes <= supported.max_temporary_bytes &&
              result.max_concurrent_calls == 1,
          "Bundle resource limits exceed the Core safety ceiling");
  return result;
}

std::shared_ptr<const internal::BundleData> parse_bundle(std::vector<BundleFile> input_files) {
  require(!input_files.empty(), "Model bundle is empty");
  require(input_files.size() <= 64, "Model bundle contains too many files");
  std::unordered_map<std::string, SharedBytes> files;
  files.reserve(input_files.size());
  std::uint64_t total_bytes = 0;
  for (auto& file : input_files) {
    require(is_normalized_path(file.path), "Bundle file path is invalid", file.path);
    require(static_cast<bool>(file.bytes), "Bundle file has null byte storage", file.path);
    require(file.bytes->size() <= 256ull * 1024 * 1024,
            "Bundle file exceeds its size limit", file.path);
    require(internal::checked_add<std::uint64_t>(
                total_bytes, static_cast<std::uint64_t>(file.bytes->size()), &total_bytes) &&
                total_bytes <= 512ull * 1024 * 1024,
            "Model bundle exceeds its total size limit");
    require(files.emplace(std::move(file.path), std::move(file.bytes)).second,
            "Bundle contains a duplicate file path");
  }

  validate_checksum_inventory(files);
  const auto manifest = parse_json_file(files, "manifest.json", 1024 * 1024);
  const auto schema_version = required<std::string>(manifest, "schemaVersion", "manifest");
  require(schema_version == "1.0" || schema_version == "1.1" ||
              schema_version == "1.2",
          "Unsupported manifest schema version", schema_version);
  const auto bundle_id = required<std::string>(manifest, "bundleId", "manifest");
  require(!bundle_id.empty() && bundle_id.size() <= 128, "Bundle ID is invalid");
  require(required<std::string>(manifest, "family", "manifest") == "PP-OCRv6",
          "Unsupported model family");

  const auto& compatibility = manifest.at("coreCompatibility");
  const auto minimum_version = parse_semantic_version(
      required<std::string>(compatibility, "minimum", "coreCompatibility"),
      "coreCompatibility.minimum");
  const auto core_version = parse_semantic_version(LIGHT_OCR_VERSION, "coreVersion");
  require(!version_less(core_version, minimum_version),
          "Bundle requires a newer light-ocr core version");
  require(core_version.major <=
              required<std::uint32_t>(compatibility, "maximumMajor", "coreCompatibility"),
          "Bundle does not support this core major version");

  const auto& upstream = manifest.at("upstream");
  require(required<std::string>(upstream, "repository", "upstream") ==
              "https://github.com/PaddlePaddle/PaddleOCR" &&
              required<std::string>(upstream, "release", "upstream") == "v3.7.0" &&
              required<std::string>(upstream, "revision", "upstream") ==
                  "b03f46425e8ff4442b268ce449e3eef758146cd4",
          "Bundle upstream identity is unsupported");

  const auto& capabilities = manifest.at("capabilities");
  require(required<bool>(capabilities, "detection", "capabilities") &&
              required<bool>(capabilities, "recognition", "capabilities"),
          "Detection and recognition capabilities are required");
  require(!required<bool>(capabilities, "textlineOrientation", "capabilities"),
          "Text-line orientation is unsupported in the initial Core bundle");

  const auto& models = manifest.at("models");
  const auto& detection_model = models.at("detection");
  const auto& recognition_model = models.at("recognition");
  const auto detection_model_id =
      required<std::string>(detection_model, "id", "models.detection");
  const auto recognition_model_id =
      required<std::string>(recognition_model, "id", "models.recognition");
  if (detection_model_id != "PP-OCRv6_small_det_onnx") {
    unsupported_model("Unsupported detection model");
  }
  if (recognition_model_id != "PP-OCRv6_small_rec_onnx") {
    unsupported_model("Unsupported recognition model");
  }
  require(required<std::string>(detection_model, "sourceRevision", "models.detection") ==
              "28fe5895c24fd108c19eb3e8479f4ab385fbfc62" &&
              required<std::uint32_t>(detection_model, "inputRank", "models.detection") == 4 &&
              required<std::vector<std::uint32_t>>(detection_model, "outputRanks",
                                                   "models.detection") ==
                  std::vector<std::uint32_t>({3, 4}),
          "Detection model identity or tensor declaration is unsupported");
  require(required<std::string>(recognition_model, "sourceRevision", "models.recognition") ==
              "b8f84f0b80c529de40b4fbb3544b84fa7233a513" &&
              required<std::uint32_t>(recognition_model, "inputRank", "models.recognition") == 4 &&
              required<std::uint32_t>(recognition_model, "outputRank", "models.recognition") == 3,
          "Recognition model identity or tensor declaration is unsupported");
  const auto detection_model_path =
      required<std::string>(detection_model, "modelPath", "models.detection");
  const auto recognition_model_path =
      required<std::string>(recognition_model, "modelPath", "models.recognition");
  const auto detection_config_path =
      required<std::string>(detection_model, "configPath", "models.detection");
  const auto recognition_config_path =
      required<std::string>(recognition_model, "configPath", "models.recognition");
  const auto recognition_dictionary_path =
      required<std::string>(recognition_model, "dictionaryPath", "models.recognition");
  require(is_normalized_path(detection_model_path) &&
              is_normalized_path(recognition_model_path) &&
              is_normalized_path(detection_config_path) &&
              is_normalized_path(recognition_config_path) &&
              is_normalized_path(recognition_dictionary_path),
          "Model manifest contains an invalid path");
  file_at(files, detection_model_path);
  file_at(files, recognition_model_path);
  file_at(files, detection_config_path);
  file_at(files, recognition_config_path);
  file_at(files, recognition_dictionary_path);

  const auto licenses = required<std::vector<std::string>>(manifest, "licenses", "manifest");
  require(std::find(licenses.begin(), licenses.end(), "Apache-2.0") != licenses.end(),
          "Bundle does not declare the required Apache-2.0 license");
  file_at(files, "LICENSES/PaddleOCR-Apache-2.0.txt");
  file_at(files, "LICENSES/MODEL-NOTICE.md");

  validate_file_inventory(manifest, files);
  const auto& file_inventory = manifest.at("files");
  const auto detection_model_sha256 = required<std::string>(
      file_inventory.at(detection_model_path), "sha256",
      "files." + detection_model_path);
  const auto recognition_model_sha256 = required<std::string>(
      file_inventory.at(recognition_model_path), "sha256",
      "files." + recognition_model_path);
  const auto normalized_path =
      required<std::string>(manifest, "normalizedConfigPath", "manifest");
  require(is_normalized_path(normalized_path), "Normalized configuration path is invalid",
          normalized_path);
  const auto normalized = parse_json_file(files, normalized_path, 2 * 1024 * 1024);
  const auto normalized_schema =
      required<std::string>(normalized, "schemaVersion", "normalizedConfig");
  require(normalized_schema == "1.0" || normalized_schema == "1.1" ||
              normalized_schema == "1.2",
          "Unsupported normalized configuration schema", normalized_schema);
  require(required<std::string>(normalized, "bundleId", "normalizedConfig") == bundle_id,
          "Normalized configuration bundle ID does not match manifest");

  auto data = std::make_shared<internal::BundleData>();
  data->id = bundle_id;
  data->schema_version = schema_version;
  data->normalized_config_schema_version = normalized_schema;
  data->detection_model_path = detection_model_path;
  data->detection_model_id = detection_model_id;
  data->detection_model_sha256 = detection_model_sha256;
  data->recognition_model_path = recognition_model_path;
  data->recognition_model_id = recognition_model_id;
  data->recognition_model_sha256 = recognition_model_sha256;
  data->files = std::move(files);
  data->detection = parse_detection(normalized, normalized_schema);
  data->tiled_detection =
      parse_tiled_detection(normalized, normalized_schema, data->detection);
  const auto runtime_defaults =
      parse_runtime_defaults(normalized, normalized_schema, data->detection);
  data->default_detection_strategy = runtime_defaults.detection_strategy;
  data->default_detection_max_side = runtime_defaults.detection_max_side;
  data->geometry = parse_geometry(normalized);
  data->recognition =
      parse_recognition(normalized, data->files, recognition_dictionary_path,
                        runtime_defaults);
  data->apple_provider =
      parse_apple_provider(manifest, schema_version, data->files);
  data->webgpu_provider = parse_webgpu_provider(
      manifest, schema_version, data->files, detection_model_id,
      detection_model_sha256, recognition_model_id,
      recognition_model_sha256);
  const std::size_t provider_count =
      static_cast<std::size_t>(data->apple_provider.has_value()) +
      static_cast<std::size_t>(data->webgpu_provider.has_value());
  std::size_t declared_provider_count = 0;
  if (manifest.contains("providers")) {
    require(manifest.at("providers").is_object(),
            "Manifest providers must be an object");
    declared_provider_count = manifest.at("providers").size();
  }
  require((schema_version == "1.0" && provider_count == 0) ||
              (schema_version == "1.1" && provider_count == 1 &&
               data->apple_provider.has_value()) ||
              (schema_version == "1.2" && data->webgpu_provider.has_value() &&
               provider_count >= 1 && provider_count <= 2),
          "Manifest provider payloads do not match the schema contract");
  require(declared_provider_count == provider_count,
          "Manifest contains an unknown provider payload");
  data->limits = parse_limits(normalized, normalized_schema);
  data->capabilities =
      Capabilities{true, true, false, data->tiled_detection.has_value()};
  require(data->detection.max_side_limit <= data->limits.max_detection_side &&
              data->default_detection_max_side <= data->limits.max_detection_side &&
              data->detection.max_candidates <= data->limits.max_detection_candidates &&
              (!data->tiled_detection ||
               (data->tiled_detection->tile_side <= data->limits.max_detection_side &&
                data->limits.max_detection_tiles > 0)) &&
              data->recognition.default_batch_size <=
                  data->limits.max_recognition_batch_size &&
              data->recognition.maximum_batch_size <= data->limits.max_recognition_batch_size &&
              data->recognition.maximum_tensor_width <= data->limits.max_recognition_width,
          "Normalized configuration exceeds its resource limits");
  return data;
}

}  // namespace

Result<ModelBundle> ModelBundle::create(std::vector<BundleFile> files) {
  try {
    return Result<ModelBundle>::success(ModelBundle(parse_bundle(std::move(files))));
  } catch (const BundleFailure& failure) {
    return Result<ModelBundle>::failure(Error{failure.code, failure.what(), failure.detail});
  } catch (const std::exception& exception) {
    return Result<ModelBundle>::failure(
        Error{ErrorCode::invalid_model_bundle, "Unexpected model bundle validation failure",
              exception.what()});
  } catch (...) {
    return Result<ModelBundle>::failure(
        Error{ErrorCode::internal_error, "Unknown model bundle validation failure", {}});
  }
}

ModelBundle::ModelBundle(std::shared_ptr<const internal::BundleData> data) : data_(std::move(data)) {}
ModelBundle::ModelBundle(ModelBundle&&) noexcept = default;
ModelBundle& ModelBundle::operator=(ModelBundle&&) noexcept = default;
ModelBundle::~ModelBundle() = default;

const std::string& ModelBundle::id() const noexcept {
  static const std::string empty;
  return data_ ? data_->id : empty;
}

const std::string& ModelBundle::schema_version() const noexcept {
  static const std::string empty;
  return data_ ? data_->schema_version : empty;
}

}  // namespace light_ocr
