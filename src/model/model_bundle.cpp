#include "light_ocr/core.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
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
  const auto& checksum_bytes = file_at(files, "SHA256SUMS");
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

internal::DetectionConfig parse_detection(const Json& root) {
  const auto& detection = root.at("detection");
  const auto& input = detection.at("input");
  require_string(input, "colorOrder", "BGR", "detection.input");
  require_string(input, "tensorLayout", "NCHW", "detection.input");
  require_string(input, "tensorType", "float32", "detection.input");

  const auto& resize = detection.at("resize");
  require_string(resize, "limitType", "min", "detection.resize");
  require_string(resize, "scaledDimensionRounding", "truncate_toward_zero", "detection.resize");
  require_string(resize, "multipleRounding", "half_to_even", "detection.resize");
  require_string(resize, "maxSideLimitOrder", "before_multiple_rounding", "detection.resize");
  require_string(resize, "interpolation", "linear", "detection.resize");

  const auto& normalize = detection.at("normalize");
  const auto& postprocess = detection.at("postprocess");
  require_string(postprocess, "algorithm", "DB", "detection.postprocess");
  require_string(postprocess, "scoreMode", "fast", "detection.postprocess");
  require_string(postprocess, "boxType", "quad", "detection.postprocess");

  internal::DetectionConfig config;
  config.limit_side_len = required_u32(resize, "limitSideLen", "detection.resize");
  config.limit_type = "min";
  config.max_side_limit = required_u32(resize, "maxSideLimit", "detection.resize");
  config.dimension_multiple = required_u32(resize, "dimensionMultiple", "detection.resize");
  config.minimum_dimension = required_u32(resize, "minimumDimension", "detection.resize");
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
    const std::string& manifest_dictionary_path) {
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
  config.default_batch_size = required_u32(batch, "defaultSize", "recognition.batch");
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

ResourceLimits parse_limits(const Json& root) {
  const auto& limits = root.at("resourceLimits");
  ResourceLimits result;
  result.max_width = required_u32(limits, "maxWidth", "resourceLimits");
  result.max_height = required_u32(limits, "maxHeight", "resourceLimits");
  result.max_pixels = required_u64(limits, "maxPixels", "resourceLimits");
  result.max_detection_side = required_u32(limits, "maxDetectionSide", "resourceLimits");
  result.max_detection_candidates =
      required_u32(limits, "maxDetectionCandidates", "resourceLimits");
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
  require(schema_version == "1.0", "Unsupported manifest schema version", schema_version);
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
  if (required<std::string>(detection_model, "id", "models.detection") !=
      "PP-OCRv6_small_det_onnx") {
    unsupported_model("Unsupported detection model");
  }
  if (required<std::string>(recognition_model, "id", "models.recognition") !=
      "PP-OCRv6_small_rec_onnx") {
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
  const auto normalized_path =
      required<std::string>(manifest, "normalizedConfigPath", "manifest");
  require(is_normalized_path(normalized_path), "Normalized configuration path is invalid",
          normalized_path);
  const auto normalized = parse_json_file(files, normalized_path, 2 * 1024 * 1024);
  require(required<std::string>(normalized, "schemaVersion", "normalizedConfig") == "1.0",
          "Unsupported normalized configuration schema");
  require(required<std::string>(normalized, "bundleId", "normalizedConfig") == bundle_id,
          "Normalized configuration bundle ID does not match manifest");

  auto data = std::make_shared<internal::BundleData>();
  data->id = bundle_id;
  data->schema_version = schema_version;
  data->detection_model_path = detection_model_path;
  data->recognition_model_path = recognition_model_path;
  data->files = std::move(files);
  data->detection = parse_detection(normalized);
  data->geometry = parse_geometry(normalized);
  data->recognition =
      parse_recognition(normalized, data->files, recognition_dictionary_path);
  data->limits = parse_limits(normalized);
  data->capabilities = Capabilities{true, true, false};
  require(data->detection.max_side_limit <= data->limits.max_detection_side &&
              data->detection.max_candidates <= data->limits.max_detection_candidates &&
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
