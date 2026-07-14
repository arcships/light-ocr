#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "light_ocr/core.hpp"
#include "light_ocr/error.hpp"
#include "test.hpp"
#include "util/sha256.hpp"

using namespace light_ocr;

namespace {

using Json = nlohmann::json;

SharedBytes bytes(const std::string& value) {
  return std::make_shared<const std::vector<std::uint8_t>>(value.begin(), value.end());
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

std::vector<BundleFile> valid_bundle_files() {
  Json dictionary = {{"schemaVersion", "1.0"}, {"characters", Json::array({"a", " "})}};
  Json normalized = {
      {"schemaVersion", "1.0"},
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
      {"detection",
       {{"input", {{"colorOrder", "BGR"}, {"tensorLayout", "NCHW"}, {"tensorType", "float32"}}},
        {"resize",
         {{"limitSideLen", 64},
          {"limitType", "min"},
          {"maxSideLimit", 4000},
          {"dimensionMultiple", 32},
          {"minimumDimension", 32},
          {"scaledDimensionRounding", "truncate_toward_zero"},
          {"multipleRounding", "half_to_even"},
          {"maxSideLimitOrder", "before_multiple_rounding"},
          {"interpolation", "linear"}}},
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
        {"batch", {{"defaultSize", 2}, {"maximumSize", 8}, {"sortByWidth", true}}},
        {"decode",
         {{"algorithm", "CTC"}, {"blankIndex", 0}, {"collapseRepeats", true},
          {"appendSpaceCharacter", true},
          {"confidence", "mean_selected_argmax_probability"},
          {"dictionaryPath", "rec/dictionary.json"}, {"dictionaryEntries", 2}}},
        {"defaultScoreThreshold", 0.0}}}};

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

}  // namespace

LIGHT_OCR_TEST(model_bundle_accepts_complete_hashed_contract) {
  auto result = ModelBundle::create(valid_bundle_files());
  EXPECT_TRUE(result);
  EXPECT_EQ(result.value().id(), "test-bundle");
  EXPECT_EQ(result.value().schema_version(), "1.0");
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
