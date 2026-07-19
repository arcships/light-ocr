#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "light_ocr/core.hpp"
#include "light_ocr/error.hpp"
#include "core/engine_factory.hpp"
#include "test.hpp"
#include "util/sha256.hpp"

using namespace light_ocr;

namespace {

using Json = nlohmann::json;

SharedBytes bytes(const std::string& value) {
  return std::make_shared<const std::vector<std::uint8_t>>(value.begin(), value.end());
}

std::string package_hash(const std::vector<BundleFile>& files,
                         const std::string& prefix) {
  std::vector<const BundleFile*> package_files;
  for (const auto& file : files) {
    if (file.path.compare(0, prefix.size(), prefix) == 0) {
      package_files.push_back(&file);
    }
  }
  std::sort(package_files.begin(), package_files.end(),
            [](const auto* left, const auto* right) {
              return left->path < right->path;
            });
  std::string inventory;
  for (const auto* file : package_files) {
    inventory += file->path.substr(prefix.size());
    inventory.push_back('\0');
    inventory += internal::sha256_hex(file->bytes->data(), file->bytes->size());
    inventory.push_back('\n');
  }
  return internal::sha256_hex(
      reinterpret_cast<const std::uint8_t*>(inventory.data()), inventory.size());
}

void refresh_checksums(std::vector<BundleFile>* files) {
  std::string sums;
  for (const auto& file : *files) {
    if (file.path == "SHA256SUMS") continue;
    sums += internal::sha256_hex(file.bytes->data(), file.bytes->size()) + "  " + file.path + "\n";
  }
  for (auto& file : *files) {
    if (file.path == "SHA256SUMS") {
      file.bytes = bytes(sums);
      return;
    }
  }
  files->push_back(BundleFile{"SHA256SUMS", bytes(sums)});
}

void replace_payload(std::vector<BundleFile>* files, const std::string& path,
                     const std::string& value) {
  for (auto& file : *files) {
    if (file.path == path) {
      file.bytes = bytes(value);
      break;
    }
  }
  for (auto& file : *files) {
    if (file.path != "manifest.json") continue;
    const auto manifest_text = std::string(file.bytes->begin(), file.bytes->end());
    auto manifest = Json::parse(manifest_text);
    manifest["files"][path] = {
        {"bytes", value.size()},
        {"sha256", internal::sha256_hex(
                       reinterpret_cast<const std::uint8_t*>(value.data()), value.size())},
    };
    file.bytes = bytes(manifest.dump());
    refresh_checksums(files);
    return;
  }
}

std::vector<BundleFile> valid_bundle_files(bool tiled = false) {
  Json dictionary = {{"schemaVersion", "1.0"}, {"characters", Json::array({"a", " "})}};
  Json normalized = {
      {"schemaVersion", "1.1"},
      {"bundleId", "test-bundle"},
      {"resourceLimits",
       {{"maxWidth", 10000},
        {"maxHeight", 10000},
        {"maxPixels", 40000000},
        {"maxDetectionSide", 4000},
        {"maxDetectionCandidates", 3000},
        {"maxRecognitionBatchSize", 8},
        {"maxRecognitionWidth", 3200},
        {"maxTemporaryBytes", 536870912},
        {"maxConcurrentCalls", 1}}},
      {"sourceDetectionResize",
       {{"limitSideLen", 64},
        {"limitType", "min"},
        {"maxSideLimit", 4000},
        {"dimensionMultiple", 32},
        {"minimumDimension", 32},
        {"scaledDimensionRounding", "truncate_toward_zero"},
        {"multipleRounding", "half_to_even"},
        {"maxSideLimitOrder", "before_multiple_rounding"},
        {"interpolation", "linear"}}},
      {"runtimeDefaults",
       {{"detection",
         {{"strategy", "bounded"},
          {"maxSide", 960},
          {"minimumShortSide", 64},
          {"dimensionMultipleRounding", "ceil"}}},
        {"recognitionBatchSize", 1}}},
      {"detection",
       {{"input", {{"colorOrder", "BGR"}, {"tensorLayout", "NCHW"}, {"tensorType", "float32"}}},
        {"normalize", {{"scale", 1.0 / 255}, {"mean", {0.485, 0.456, 0.406}},
                       {"std", {0.229, 0.224, 0.225}}}},
        {"postprocess",
         {{"algorithm", "DB"}, {"threshold", 0.3}, {"boxThreshold", 0.6},
          {"unclipRatio", 1.5}, {"maxCandidates", 3000}, {"useDilation", false},
          {"scoreMode", "fast"}, {"boxType", "quad"}, {"minimumBoxSide", 3}}}}},
      {"geometry",
       {{"rowBandPixels", 10}, {"perspectiveInterpolation", "cubic"},
        {"borderMode", "replicate"}, {"tallLineRatio", 1.5},
        {"tallLineRotation", "counterclockwise90"}}},
      {"recognition",
       {{"input",
         {{"colorOrder", "BGR"}, {"tensorLayout", "NCHW"}, {"tensorType", "float32"},
          {"shape", {3, 48, 320}}, {"minimumTensorWidth", 320},
          {"maximumTensorWidth", 3200}, {"tensorWidthRounding", "truncate_toward_zero"},
          {"resizedContentWidthRounding", "ceil"},
          {"batchTensorWidth", "maximum_sample_tensor_width"}, {"interpolation", "linear"}}},
        {"normalize", {{"scale", 1.0 / 255}, {"mean", {0.5, 0.5, 0.5}},
                       {"std", {0.5, 0.5, 0.5}}, {"paddingValue", 0.0}}},
        {"batch", {{"maximumSize", 8}, {"sortByWidth", true}}},
        {"decode",
         {{"algorithm", "CTC"}, {"blankIndex", 0}, {"collapseRepeats", true},
          {"appendSpaceCharacter", true},
          {"confidence", "mean_selected_argmax_probability"},
          {"dictionaryPath", "rec/dictionary.json"}, {"dictionaryEntries", 2}}},
        {"defaultScoreThreshold", 0.0}}}};

  if (tiled) {
    normalized["schemaVersion"] = "1.2";
    normalized["resourceLimits"]["maxDetectionTiles"] = 100;
    normalized["runtimeProfiles"]["tiled"] = {
        {"contractVersion", "tiled-v1"},
        {"tileSide", 1280},
        {"minimumOverlap", 128},
        {"dimensionMultiple", 32},
        {"dimensionMultipleRounding", "ceil_resize"},
        {"artificialBoundaryMargin", 32},
        {"tileOrder", "row_major"},
        {"merge",
         {{"iouThreshold", 0.5},
          {"intersectionOverSmallerThreshold", 0.8},
          {"scope", "different_overlapping_tiles"},
          {"geometry", "select_representative"},
          {"selectionOrder",
           {"not_artificial_boundary", "higher_db_score",
            "farther_from_artificial_boundary", "lower_tile_ordinal",
            "lower_candidate_ordinal"}}}},
        {"recognition", "once_after_global_merge"}};
  }

  std::vector<BundleFile> files = {
      {"normalized-config.json", bytes(normalized.dump())},
      {"det/inference.onnx", bytes("det-model")},
      {"det/inference.yml", bytes("det-yaml")},
      {"rec/inference.onnx", bytes("rec-model")},
      {"rec/inference.yml", bytes("rec-yaml")},
      {"rec/dictionary.json", bytes(dictionary.dump())},
      {"LICENSES/LICENSE.txt", bytes("license")},
      {"LICENSES/PaddleOCR-Apache-2.0.txt", bytes("apache-license")},
      {"LICENSES/MODEL-NOTICE.md", bytes("model-notice")},
  };
  Json inventory = Json::object();
  for (const auto& file : files) {
    inventory[file.path] = {
        {"bytes", file.bytes->size()},
        {"sha256", internal::sha256_hex(file.bytes->data(), file.bytes->size())},
    };
  }
  Json manifest = {
      {"schemaVersion", "1.0"},
      {"bundleId", "test-bundle"},
      {"family", "PP-OCRv6"},
      {"coreCompatibility", {{"minimum", "0.1.0"}, {"maximumMajor", 0}}},
      {"upstream",
       {{"repository", "https://github.com/PaddlePaddle/PaddleOCR"},
        {"release", "v3.7.0"},
        {"revision", "b03f46425e8ff4442b268ce449e3eef758146cd4"}}},
      {"capabilities", {{"detection", true}, {"recognition", true},
                        {"textlineOrientation", false}}},
      {"models",
       {{"detection", {{"id", "PP-OCRv6_small_det_onnx"},
                        {"sourceRevision", "28fe5895c24fd108c19eb3e8479f4ab385fbfc62"},
                        {"modelPath", "det/inference.onnx"},
                        {"configPath", "det/inference.yml"},
                        {"inputRank", 4},
                        {"outputRanks", {3, 4}}}},
        {"recognition", {{"id", "PP-OCRv6_small_rec_onnx"},
                          {"sourceRevision", "b8f84f0b80c529de40b4fbb3544b84fa7233a513"},
                          {"modelPath", "rec/inference.onnx"},
                          {"configPath", "rec/inference.yml"},
                          {"dictionaryPath", "rec/dictionary.json"},
                          {"inputRank", 4},
                          {"outputRank", 3}}}}},
      {"normalizedConfigPath", "normalized-config.json"},
      {"files", inventory},
      {"licenses", {"Apache-2.0"}},
  };
  files.push_back(BundleFile{"manifest.json", bytes(manifest.dump())});
  refresh_checksums(&files);
  return files;
}

std::vector<BundleFile> valid_apple_bundle_files() {
  auto files = valid_bundle_files(true);
  files.erase(std::remove_if(files.begin(), files.end(), [](const BundleFile& file) {
                return file.path == "SHA256SUMS";
              }),
              files.end());
  const std::string detection_root = "apple/detector.mlpackage/";
  const std::string recognition_root = "apple/recognizer.mlpackage/";
  for (const auto& root : {detection_root, recognition_root}) {
    files.push_back(BundleFile{root + "Manifest.json", bytes("manifest")});
    files.push_back(BundleFile{root + "Data/com.apple.CoreML/model.mlmodel",
                               bytes("model")});
    files.push_back(BundleFile{
        root + "Data/com.apple.CoreML/weights/weight.bin", bytes("weights")});
  }
  for (auto& file : files) {
    if (file.path != "manifest.json") continue;
    auto manifest = Json::parse(std::string(file.bytes->begin(), file.bytes->end()));
    manifest["schemaVersion"] = "1.1";
    manifest["coreCompatibility"]["minimum"] = "0.3.0";
    for (const auto& payload : files) {
      if (payload.path == "manifest.json") continue;
      manifest["files"][payload.path] = {
          {"bytes", payload.bytes->size()},
          {"sha256", internal::sha256_hex(payload.bytes->data(),
                                           payload.bytes->size())},
      };
    }
    std::vector<std::uint32_t> widths;
    for (std::uint32_t width = 320; width <= 3200; width += 32) {
      widths.push_back(width);
    }
    manifest["providers"]["apple"] = {
        {"schemaVersion", "1.1"},
        {"minimumMacOS", "15.0"},
        {"devicePolicy", "open-macos"},
        {"architectures", {"arm64", "x86_64"}},
        {"validatedDeviceFamilies", {"Apple M4"}},
        {"qualificationId", "apple-test-qualification"},
        {"detection",
         {{"modelId", "detector-fp16"},
          {"packagePath", detection_root.substr(0, detection_root.size() - 1)},
          {"packageSha256", package_hash(files, detection_root)},
          {"inputName", "x"},
          {"outputName", "output"},
          {"shapePolicy", "nchw-bounded-range-32-960-v1"},
          {"preferredComputeUnit", "ane"},
          {"strictComputeUnit", "gpu"},
          {"intelComputeUnit", "cpu+gpu"},
          {"qualifiedMLCPUOperations", {{"ios18.relu", 1}, {"pad", 1}}}}},
        {"recognition",
         {{"modelId", "recognizer-fp16"},
          {"packagePath", recognition_root.substr(0, recognition_root.size() - 1)},
          {"packageSha256", package_hash(files, recognition_root)},
          {"inputName", "x"},
          {"outputName", "output"},
          {"shapePolicy", "nchw-static-width-multiple-32-v1"},
          {"functionFormat", "w%04u"},
          {"widths", widths},
          {"widthMultiple", 32},
          {"aneMaximumWidth", 1600},
          {"runtimeWidthBuckets",
           {320, 384, 480, 544, 576, 608, 704, 736, 832, 960,
            1056, 1184, 1248, 1376, 1600, 1984, 2240, 2560, 2880,
            3200}},
          {"maximumCachedFunctions", 20},
          {"intelComputeUnit", "cpu+gpu"},
          {"qualifiedMLCPUOperations",
           {{"ios18.cast", 1}, {"ios18.conv", 3},
            {"ios18.relu", 3}, {"pad", 3}}}}},
    };
    file.bytes = bytes(manifest.dump());
    break;
  }
  refresh_checksums(&files);
  return files;
}

std::vector<BundleFile> valid_webgpu_bundle_files() {
  auto files = valid_bundle_files(true);
  files.erase(std::remove_if(files.begin(), files.end(), [](const BundleFile& file) {
                return file.path == "SHA256SUMS";
              }),
              files.end());
  const std::string detection = "webgpu-fp16-detection";
  const std::string recognition = "webgpu-fp16-recognition";
  const std::string provenance = "webgpu-fp16-provenance";
  files.push_back(BundleFile{"webgpu/det/inference.onnx", bytes(detection)});
  files.push_back(BundleFile{"webgpu/rec/inference.onnx", bytes(recognition)});
  files.push_back(BundleFile{"webgpu/provenance.json", bytes(provenance)});
  for (auto& file : files) {
    if (file.path != "manifest.json") continue;
    auto manifest = Json::parse(std::string(file.bytes->begin(), file.bytes->end()));
    manifest["schemaVersion"] = "1.2";
    manifest["coreCompatibility"]["minimum"] = "0.3.0";
    for (const auto& payload : files) {
      if (payload.path == "manifest.json") continue;
      manifest["files"][payload.path] = {
          {"bytes", payload.bytes->size()},
          {"sha256", internal::sha256_hex(payload.bytes->data(),
                                           payload.bytes->size())},
      };
    }
    manifest["providers"]["webgpu"] = {
        {"schemaVersion", "1.0"},
        {"conversionId", "onnxruntime-float16-1.24.4-20260719.1"},
        {"precision", "fp16"},
        {"graphOptimizationLevel", "extended"},
        {"cpuPartition", "allow-required"},
        {"requiredCpuOperators", {"Concat", "Gather", "Slice"}},
        {"provenancePath", "webgpu/provenance.json"},
        {"provenanceSha256",
         internal::sha256_hex(
             reinterpret_cast<const std::uint8_t*>(provenance.data()),
             provenance.size())},
        {"detection",
         {{"modelId", "PP-OCRv6_small_det_onnx_webgpu_fp16"},
          {"modelPath", "webgpu/det/inference.onnx"},
          {"modelSha256",
           internal::sha256_hex(
               reinterpret_cast<const std::uint8_t*>(detection.data()),
               detection.size())},
          {"sourceModelId", "PP-OCRv6_small_det_onnx"},
          {"sourceModelSha256",
           manifest["files"]["det/inference.onnx"]["sha256"]},
          {"tensorType", "float16"}}},
        {"recognition",
         {{"modelId", "PP-OCRv6_small_rec_onnx_webgpu_fp16"},
          {"modelPath", "webgpu/rec/inference.onnx"},
          {"modelSha256",
           internal::sha256_hex(
               reinterpret_cast<const std::uint8_t*>(recognition.data()),
               recognition.size())},
          {"sourceModelId", "PP-OCRv6_small_rec_onnx"},
          {"sourceModelSha256",
           manifest["files"]["rec/inference.onnx"]["sha256"]},
          {"tensorType", "float16"}}},
    };
    file.bytes = bytes(manifest.dump());
    break;
  }
  refresh_checksums(&files);
  return files;
}

}  // namespace

LIGHT_OCR_TEST(model_bundle_accepts_complete_hashed_contract) {
  auto result = ModelBundle::create(valid_bundle_files());
  EXPECT_TRUE(result);
  EXPECT_EQ(result.value().id(), "test-bundle");
  EXPECT_EQ(result.value().schema_version(), "1.0");
}

LIGHT_OCR_TEST(model_bundle_accepts_tiled_v1_normalized_contract) {
  auto result = ModelBundle::create(valid_bundle_files(true));
  if (!result) {
    light_ocr::test::fail("result", __FILE__, __LINE__,
                          result.error().message + ": " + result.error().detail);
  }
}

LIGHT_OCR_TEST(model_bundle_accepts_locked_apple_provider_contract) {
  auto result = ModelBundle::create(valid_apple_bundle_files());
  if (!result) {
    light_ocr::test::fail("result", __FILE__, __LINE__,
                          result.error().message + ": " + result.error().detail);
  }
  EXPECT_EQ(result.value().schema_version(), "1.1");
}

LIGHT_OCR_TEST(model_bundle_accepts_locked_webgpu_fp16_provider_contract) {
  auto result = ModelBundle::create(valid_webgpu_bundle_files());
  if (!result) {
    light_ocr::test::fail("result", __FILE__, __LINE__,
                          result.error().message + ": " + result.error().detail);
  }
  EXPECT_EQ(result.value().schema_version(), "1.2");
}

LIGHT_OCR_TEST(model_bundle_rejects_mutated_webgpu_fp16_source_binding) {
  auto files = valid_webgpu_bundle_files();
  for (auto& file : files) {
    if (file.path != "manifest.json") continue;
    auto manifest = Json::parse(std::string(file.bytes->begin(), file.bytes->end()));
    manifest["providers"]["webgpu"]["detection"]["sourceModelSha256"] =
        std::string(64, '0');
    file.bytes = bytes(manifest.dump());
    break;
  }
  refresh_checksums(&files);
  auto result = ModelBundle::create(std::move(files));
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::invalid_model_bundle);
}

LIGHT_OCR_TEST(model_bundle_rejects_unknown_provider_payload) {
  auto files = valid_webgpu_bundle_files();
  for (auto& file : files) {
    if (file.path != "manifest.json") continue;
    auto manifest = Json::parse(std::string(file.bytes->begin(), file.bytes->end()));
    manifest["providers"]["future"] = Json::object();
    file.bytes = bytes(manifest.dump());
    break;
  }
  refresh_checksums(&files);
  auto result = ModelBundle::create(std::move(files));
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::invalid_model_bundle);
}

LIGHT_OCR_TEST(webgpu_fp16_is_not_a_public_execution_profile) {
  auto bundle = ModelBundle::create(valid_webgpu_bundle_files());
  EXPECT_TRUE(bundle);
  EngineOptions options;
  options.execution.provider = ExecutionProvider::webgpu;
  options.execution.precision = Precision::fp16;
  options.execution.cpu_partition = CpuPartition::forbid;
  internal::RuntimePolicy policy;
  policy.id = "test-webgpu-v1";
  policy.version = 1;
  policy.ordered_candidates = {"webgpu", "cpu"};
  policy.available_providers = {"webgpu", "cpu"};
  policy.provider_qualification_ids = {"test-webgpu-v1", "test-cpu-v1"};
  auto engine = internal::EngineFactory::create(
      std::move(bundle).value(), options, std::move(policy));
  EXPECT_FALSE(engine);
  EXPECT_EQ(engine.error().code, ErrorCode::invalid_argument);
  EXPECT_EQ(engine.error().message, "Execution options are unsupported");
  EXPECT_FALSE(engine.error().creation_trace.has_value());
}

LIGHT_OCR_TEST(model_bundle_rejects_schema_1_1_without_apple_provider) {
  auto files = valid_bundle_files(true);
  for (auto& file : files) {
    if (file.path != "manifest.json") continue;
    auto manifest = Json::parse(std::string(file.bytes->begin(), file.bytes->end()));
    manifest["schemaVersion"] = "1.1";
    manifest["coreCompatibility"]["minimum"] = "0.3.0";
    file.bytes = bytes(manifest.dump());
    break;
  }
  refresh_checksums(&files);
  auto result = ModelBundle::create(std::move(files));
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::invalid_model_bundle);
}

LIGHT_OCR_TEST(model_bundle_rejects_unknown_validated_apple_device_family) {
  auto files = valid_apple_bundle_files();
  for (auto& file : files) {
    if (file.path != "manifest.json") continue;
    auto manifest = Json::parse(std::string(file.bytes->begin(), file.bytes->end()));
    manifest["providers"]["apple"]["validatedDeviceFamilies"] = {"Apple M9"};
    file.bytes = bytes(manifest.dump());
    break;
  }
  refresh_checksums(&files);
  auto result = ModelBundle::create(std::move(files));
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::invalid_model_bundle);
}

LIGHT_OCR_TEST(model_bundle_rejects_unknown_apple_device_policy) {
  auto files = valid_apple_bundle_files();
  for (auto& file : files) {
    if (file.path != "manifest.json") continue;
    auto manifest = Json::parse(std::string(file.bytes->begin(), file.bytes->end()));
    manifest["providers"]["apple"]["devicePolicy"] = "future-policy";
    file.bytes = bytes(manifest.dump());
    break;
  }
  refresh_checksums(&files);
  auto result = ModelBundle::create(std::move(files));
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::invalid_model_bundle);
}

LIGHT_OCR_TEST(model_bundle_rejects_incomplete_apple_architectures) {
  auto files = valid_apple_bundle_files();
  for (auto& file : files) {
    if (file.path != "manifest.json") continue;
    auto manifest = Json::parse(std::string(file.bytes->begin(), file.bytes->end()));
    manifest["providers"]["apple"]["architectures"] = {"arm64"};
    file.bytes = bytes(manifest.dump());
    break;
  }
  refresh_checksums(&files);
  auto result = ModelBundle::create(std::move(files));
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::invalid_model_bundle);
}

LIGHT_OCR_TEST(model_bundle_rejects_mutated_apple_runtime_width_buckets) {
  auto files = valid_apple_bundle_files();
  for (auto& file : files) {
    if (file.path != "manifest.json") continue;
    auto manifest = Json::parse(std::string(file.bytes->begin(), file.bytes->end()));
    manifest["providers"]["apple"]["recognition"]["runtimeWidthBuckets"][0] =
        352;
    file.bytes = bytes(manifest.dump());
    break;
  }
  refresh_checksums(&files);
  auto result = ModelBundle::create(std::move(files));
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::invalid_model_bundle);
}

LIGHT_OCR_TEST(old_normalized_bundle_rejects_tiled_engine_before_session_load) {
  auto bundle = ModelBundle::create(valid_bundle_files());
  EXPECT_TRUE(bundle);
  EngineOptions options;
  options.detection.strategy = DetectionStrategy::tiled;
  auto engine = Engine::create(std::move(bundle).value(), options);
  EXPECT_FALSE(engine);
  EXPECT_EQ(engine.error().code, ErrorCode::unsupported_capability);
}

LIGHT_OCR_TEST(model_bundle_rejects_mutated_tiled_v1_contract) {
  auto threshold = valid_bundle_files(true);
  for (const auto& file : threshold) {
    if (file.path != "normalized-config.json") continue;
    auto normalized = Json::parse(std::string(file.bytes->begin(), file.bytes->end()));
    normalized["runtimeProfiles"]["tiled"]["merge"]["iouThreshold"] = 0.51;
    replace_payload(&threshold, "normalized-config.json", normalized.dump());
    break;
  }
  auto threshold_result = ModelBundle::create(std::move(threshold));
  EXPECT_FALSE(threshold_result);
  EXPECT_EQ(threshold_result.error().code, ErrorCode::invalid_model_bundle);

  auto missing_limit = valid_bundle_files(true);
  for (const auto& file : missing_limit) {
    if (file.path != "normalized-config.json") continue;
    auto normalized = Json::parse(std::string(file.bytes->begin(), file.bytes->end()));
    normalized["resourceLimits"].erase("maxDetectionTiles");
    replace_payload(&missing_limit, "normalized-config.json", normalized.dump());
    break;
  }
  auto limit_result = ModelBundle::create(std::move(missing_limit));
  EXPECT_FALSE(limit_result);
  EXPECT_EQ(limit_result.error().code, ErrorCode::invalid_model_bundle);
}

LIGHT_OCR_TEST(model_bundle_rejects_payload_hash_mismatch) {
  auto files = valid_bundle_files();
  for (auto& file : files) {
    if (file.path == "det/inference.onnx") file.bytes = bytes("bad-model");
  }
  auto result = ModelBundle::create(std::move(files));
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::model_integrity_failed);
}

LIGHT_OCR_TEST(model_bundle_requires_complete_checksum_inventory) {
  auto missing = valid_bundle_files();
  missing.erase(std::remove_if(missing.begin(), missing.end(), [](const BundleFile& file) {
                  return file.path == "SHA256SUMS";
                }),
                missing.end());
  auto missing_result = ModelBundle::create(std::move(missing));
  EXPECT_FALSE(missing_result);
  EXPECT_EQ(missing_result.error().code, ErrorCode::invalid_model_bundle);

  auto stale = valid_bundle_files();
  for (auto& file : stale) {
    if (file.path == "SHA256SUMS") file.bytes = bytes(std::string(64, '0') + "  manifest.json\n");
  }
  auto stale_result = ModelBundle::create(std::move(stale));
  EXPECT_FALSE(stale_result);
  EXPECT_EQ(stale_result.error().code, ErrorCode::model_integrity_failed);
}

LIGHT_OCR_TEST(model_bundle_rejects_parent_traversal) {
  auto files = valid_bundle_files();
  files.push_back(BundleFile{"../escape", bytes("x")});
  auto result = ModelBundle::create(std::move(files));
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::invalid_model_bundle);
}

LIGHT_OCR_TEST(model_bundle_rejects_unlisted_duplicate_and_null_payloads) {
  auto unlisted = valid_bundle_files();
  unlisted.push_back(BundleFile{"extra.bin", bytes("x")});
  auto unlisted_result = ModelBundle::create(std::move(unlisted));
  EXPECT_FALSE(unlisted_result);
  EXPECT_EQ(unlisted_result.error().code, ErrorCode::invalid_model_bundle);

  auto duplicate = valid_bundle_files();
  duplicate.push_back(duplicate.front());
  auto duplicate_result = ModelBundle::create(std::move(duplicate));
  EXPECT_FALSE(duplicate_result);
  EXPECT_EQ(duplicate_result.error().code, ErrorCode::invalid_model_bundle);

  auto null_payload = valid_bundle_files();
  null_payload.front().bytes.reset();
  auto null_result = ModelBundle::create(std::move(null_payload));
  EXPECT_FALSE(null_result);
  EXPECT_EQ(null_result.error().code, ErrorCode::invalid_model_bundle);
}

LIGHT_OCR_TEST(model_bundle_rejects_correctly_hashed_invalid_configuration) {
  auto malformed = valid_bundle_files();
  replace_payload(&malformed, "normalized-config.json", "{");
  auto malformed_result = ModelBundle::create(std::move(malformed));
  EXPECT_FALSE(malformed_result);
  EXPECT_EQ(malformed_result.error().code, ErrorCode::invalid_model_bundle);

  auto mismatch = valid_bundle_files();
  for (const auto& file : mismatch) {
    if (file.path != "normalized-config.json") continue;
    auto normalized = Json::parse(std::string(file.bytes->begin(), file.bytes->end()));
    normalized["recognition"]["decode"]["dictionaryEntries"] = 3;
    replace_payload(&mismatch, "normalized-config.json", normalized.dump());
    break;
  }
  auto mismatch_result = ModelBundle::create(std::move(mismatch));
  EXPECT_FALSE(mismatch_result);
  EXPECT_EQ(mismatch_result.error().code, ErrorCode::invalid_model_bundle);
}

LIGHT_OCR_TEST(model_bundle_rejects_unknown_schema_and_float_overflow) {
  auto unknown_schema = valid_bundle_files();
  for (auto& file : unknown_schema) {
    if (file.path != "manifest.json") continue;
    auto manifest = Json::parse(std::string(file.bytes->begin(), file.bytes->end()));
    manifest["schemaVersion"] = "1.future";
    file.bytes = bytes(manifest.dump());
    break;
  }
  refresh_checksums(&unknown_schema);
  auto schema_result = ModelBundle::create(std::move(unknown_schema));
  EXPECT_FALSE(schema_result);
  EXPECT_EQ(schema_result.error().code, ErrorCode::invalid_model_bundle);

  auto float_overflow = valid_bundle_files();
  for (const auto& file : float_overflow) {
    if (file.path != "normalized-config.json") continue;
    auto normalized = Json::parse(std::string(file.bytes->begin(), file.bytes->end()));
    normalized["detection"]["normalize"]["mean"][0] = 1e100;
    replace_payload(&float_overflow, "normalized-config.json", normalized.dump());
    break;
  }
  auto overflow_result = ModelBundle::create(std::move(float_overflow));
  EXPECT_FALSE(overflow_result);
  EXPECT_EQ(overflow_result.error().code, ErrorCode::invalid_model_bundle);
}

LIGHT_OCR_TEST(model_bundle_rejects_unsafe_limits_and_incompatible_recognition_shape) {
  auto unsafe_limits = valid_bundle_files();
  for (const auto& file : unsafe_limits) {
    if (file.path != "normalized-config.json") continue;
    auto normalized = Json::parse(std::string(file.bytes->begin(), file.bytes->end()));
    normalized["resourceLimits"]["maxWidth"] = 10001;
    replace_payload(&unsafe_limits, "normalized-config.json", normalized.dump());
    break;
  }
  auto limits_result = ModelBundle::create(std::move(unsafe_limits));
  EXPECT_FALSE(limits_result);
  EXPECT_EQ(limits_result.error().code, ErrorCode::invalid_model_bundle);

  auto incompatible_shape = valid_bundle_files();
  for (const auto& file : incompatible_shape) {
    if (file.path != "normalized-config.json") continue;
    auto normalized = Json::parse(std::string(file.bytes->begin(), file.bytes->end()));
    normalized["recognition"]["input"]["shape"][1] = 64;
    replace_payload(&incompatible_shape, "normalized-config.json", normalized.dump());
    break;
  }
  auto shape_result = ModelBundle::create(std::move(incompatible_shape));
  EXPECT_FALSE(shape_result);
  EXPECT_EQ(shape_result.error().code, ErrorCode::invalid_model_bundle);
}

LIGHT_OCR_TEST(model_bundle_rejects_incompatible_core_and_unsupported_model) {
  auto incompatible = valid_bundle_files();
  for (auto& file : incompatible) {
    if (file.path != "manifest.json") continue;
    auto manifest = Json::parse(std::string(file.bytes->begin(), file.bytes->end()));
    manifest["coreCompatibility"]["minimum"] = "99.0.0";
    file.bytes = bytes(manifest.dump());
    break;
  }
  refresh_checksums(&incompatible);
  auto incompatible_result = ModelBundle::create(std::move(incompatible));
  EXPECT_FALSE(incompatible_result);
  EXPECT_EQ(incompatible_result.error().code, ErrorCode::invalid_model_bundle);

  auto unsupported = valid_bundle_files();
  for (auto& file : unsupported) {
    if (file.path != "manifest.json") continue;
    auto manifest = Json::parse(std::string(file.bytes->begin(), file.bytes->end()));
    manifest["models"]["detection"]["id"] = "unknown-detector";
    file.bytes = bytes(manifest.dump());
    break;
  }
  refresh_checksums(&unsupported);
  auto unsupported_result = ModelBundle::create(std::move(unsupported));
  EXPECT_FALSE(unsupported_result);
  EXPECT_EQ(unsupported_result.error().code, ErrorCode::unsupported_model);
}

LIGHT_OCR_TEST(model_bundle_rejects_excessive_file_count_before_parsing) {
  std::vector<BundleFile> files;
  for (std::size_t index = 0; index < 65; ++index) {
    files.push_back(BundleFile{"file-" + std::to_string(index), bytes("x")});
  }
  auto result = ModelBundle::create(std::move(files));
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::invalid_model_bundle);
}
