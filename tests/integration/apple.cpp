#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "common/bundle_files.hpp"
#include "inference/coreml/backend.hpp"
#include "light_ocr/core.hpp"

namespace {

using light_ocr::CpuPartition;
using light_ocr::Engine;
using light_ocr::EngineOptions;
using light_ocr::ExecutionProvider;
using light_ocr::ImageView;
using light_ocr::ModelBundle;
using light_ocr::PixelFormat;
using light_ocr::Precision;
using light_ocr::SessionFallback;
namespace fs = std::filesystem;

EngineOptions apple_options(CpuPartition partition, SessionFallback fallback) {
  EngineOptions options;
  options.execution.provider = ExecutionProvider::apple;
  options.execution.session_fallback = fallback;
  options.execution.cpu_partition = partition;
  options.execution.precision = Precision::fp16;
  options.detection.strategy = light_ocr::DetectionStrategy::bounded;
  options.recognition_batch_size = 1;
  return options;
}

std::unique_ptr<Engine> create_engine(const std::string& bundle_path,
                                      CpuPartition partition,
                                      SessionFallback fallback) {
  auto bundle = ModelBundle::create(
      light_ocr::tools::load_bundle_directory(bundle_path));
  if (!bundle) {
    throw std::runtime_error(bundle.error().message + ": " +
                             bundle.error().detail);
  }
  auto engine = Engine::create(
      std::move(bundle).value(), apple_options(partition, fallback));
  if (!engine) {
    throw std::runtime_error(engine.error().message + ": " +
                             engine.error().detail);
  }
  return std::move(engine).value();
}

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void require_hello(Engine* engine, const std::string& pixels_path,
                   const std::string& compute_unit) {
  auto pixels = light_ocr::tools::read_binary_file(pixels_path);
  const ImageView image{pixels.data(), pixels.size(), 800, 180, 2400,
                        PixelFormat::bgr8};
  light_ocr::RecognizeOptions options;
  options.include_diagnostics = true;
  auto result = engine->recognize(image, options);
  if (!result) {
    throw std::runtime_error(result.error().message + ": " +
                             result.error().detail);
  }
  require(result.value().lines.size() == 1,
          "Apple provider did not return one golden line");
  require(result.value().lines.front().text == "HELLO 123",
          "Apple provider changed the golden text");
  require(result.value().diagnostics.has_value() &&
              result.value().diagnostics->recognition_batch_shapes.size() == 1 &&
              result.value().diagnostics->recognition_batch_shapes.front()
                      .compute_unit == compute_unit &&
              !result.value().diagnostics->recognition_batch_shapes.front()
                   .model_id.empty() &&
              !result.value().diagnostics->recognition_batch_shapes.front()
                   .shape_bucket.empty(),
          "Apple recognition route diagnostics are invalid");
}

void require_wide_recognizer(const fs::path& bundle_path) {
  std::ifstream manifest_stream(bundle_path / "manifest.json", std::ios::binary);
  if (!manifest_stream) throw std::runtime_error("Apple manifest is unavailable");
  nlohmann::json manifest;
  manifest_stream >> manifest;
  const auto& provider = manifest.at("providers").at("apple");
  const auto& recognition = provider.at("recognition");
  const auto package_relative = recognition.at("packagePath").get<std::string>();
  const auto package_root = bundle_path / package_relative;

  light_ocr::internal::AppleModelPackage package;
  package.root_path = package_relative;
  package.package_sha256 = recognition.at("packageSha256").get<std::string>();
  package.input_name = recognition.at("inputName").get<std::string>();
  package.output_name = recognition.at("outputName").get<std::string>();
  package.qualification_id = provider.at("qualificationId").get<std::string>();
  package.device_policy = provider.at("devicePolicy").get<std::string>();
  package.architectures =
      provider.at("architectures").get<std::vector<std::string>>();
  package.validated_device_families =
      provider.at("validatedDeviceFamilies").get<std::vector<std::string>>();
  package.recognition_width_multiple =
      recognition.at("widthMultiple").get<std::uint32_t>();
  package.recognition_ane_maximum_width =
      recognition.at("aneMaximumWidth").get<std::uint32_t>();
  package.maximum_cached_functions =
      recognition.at("maximumCachedFunctions").get<std::uint32_t>();
  for (const auto& entry : fs::recursive_directory_iterator(package_root)) {
    if (!entry.is_regular_file()) continue;
    package.files.push_back(light_ocr::internal::ModelPackageFile{
        fs::relative(entry.path(), package_root).generic_string(),
        std::make_shared<const std::vector<std::uint8_t>>(
            light_ocr::tools::read_binary_file(entry.path()))});
  }

  light_ocr::internal::InferenceSessionConfig config;
  config.provider = ExecutionProvider::apple;
  config.cpu_partition = CpuPartition::allow;
  config.precision = Precision::fp16;
  config.model_id = recognition.at("modelId").get<std::string>();
  config.model_sha256 = package.package_sha256;
  config.shape_policy = recognition.at("shapePolicy").get<std::string>();
  config.apple_package = std::move(package);
  auto session = light_ocr::internal::CoreMlSession::create(
      config, light_ocr::internal::ModelKind::recognition);
  if (!session) {
    throw std::runtime_error(session.error().message + ": " +
                             session.error().detail);
  }
  constexpr std::int64_t width = 3200;
  const std::vector<std::int64_t> shape = {1, 3, 48, width};
  std::vector<float> values(static_cast<std::size_t>(3 * 48 * width));
  auto output = session.value()->run(values, shape);
  if (!output) {
    throw std::runtime_error(output.error().message + ": " +
                             output.error().detail);
  }
  require(output.value().shape().size() == 3 &&
              output.value().shape()[0] == 1 &&
              output.value().shape()[1] == 400 &&
              output.value().shape()[2] > 1 && output.value().size() > 400,
          "Wide Core ML recognizer output is invalid");
}

}  // namespace

int main() {
  const char* bundle_path = std::getenv("LIGHT_OCR_APPLE_MODEL_BUNDLE");
  const char* pixels_path = std::getenv("LIGHT_OCR_APPLE_TEST_PIXELS");
  if (bundle_path == nullptr || bundle_path[0] == '\0' ||
      pixels_path == nullptr || pixels_path[0] == '\0') {
    std::cout << "SKIP Apple model bundle is not available\n";
    return 77;
  }
  try {
    auto legacy_bundle = ModelBundle::create(
        light_ocr::tools::load_bundle_directory(bundle_path));
    require(static_cast<bool>(legacy_bundle),
            "Legacy fallback rejection bundle did not validate");
    auto legacy_fallback = Engine::create(
        std::move(legacy_bundle).value(),
        apple_options(CpuPartition::allow, SessionFallback::cpu));
    require(!legacy_fallback &&
                legacy_fallback.error().code ==
                    light_ocr::ErrorCode::invalid_argument,
            "Legacy explicit Apple CPU fallback was not rejected");

    require_wide_recognizer(bundle_path);

    auto interactive = create_engine(bundle_path, CpuPartition::allow,
                                     SessionFallback::error);
    const auto& interactive_info = interactive->info();
    const bool has_neural_engine =
        light_ocr::internal::coreml_device_has_neural_engine();
    const bool expected_validated =
        interactive_info.execution.detection.device_family.rfind("Apple M4", 0) == 0;
    require(interactive_info.execution_provider == "CoreML",
            "Interactive engine did not select Core ML");
    require(interactive_info.execution.provider_capabilities.size() == 2 &&
                interactive_info.execution.provider_capabilities[1].provider ==
                    "apple" &&
                interactive_info.execution.provider_capabilities[1]
                    .package_included &&
                interactive_info.execution.provider_capabilities[1]
                    .device_available &&
                interactive_info.execution.provider_capabilities[1]
                    .device_validated == expected_validated,
            "Apple capability report is invalid");
    require(interactive_info.execution.detection.actual_provider_chain ==
                (has_neural_engine
                     ? std::vector<std::string>{"CoreML(MLNeuralEngine,MLCPU)"}
                     : std::vector<std::string>{"CoreML(MLCPU,MLGPU)"}),
            "Interactive detector routing is invalid");
    require(interactive_info.execution.recognition.actual_provider_chain ==
                (has_neural_engine
                     ? std::vector<std::string>{
                           "CoreML(MLNeuralEngine,MLCPU)", "CoreML(MLGPU)"}
                     : std::vector<std::string>{"CoreML(MLCPU,MLGPU)"}),
            "Interactive recognizer routing is invalid");
    require(!interactive_info.execution.detection.qualification_id.empty() &&
                interactive_info.execution.detection.qualification_id ==
                    interactive_info.execution.recognition.qualification_id,
            "Apple qualification identity is missing or inconsistent");
    require(interactive_info.execution.detection.device_validated ==
                expected_validated &&
                interactive_info.execution.recognition.device_validated ==
                    expected_validated,
            "Apple validation status is not observable");
    require(!interactive_info.execution.detection.device_family.empty() &&
                !interactive_info.execution.detection.operating_system.empty(),
            "Apple device family or operating system is not observable");
    require_hello(interactive.get(), pixels_path,
                  has_neural_engine ? "ane" : "gpu");
    interactive->close();

    if (has_neural_engine) {
      auto strict = create_engine(bundle_path, CpuPartition::forbid,
                                  SessionFallback::error);
      require(strict->info().execution.detection.actual_provider_chain ==
                  std::vector<std::string>{"CoreML(MLGPU)"} &&
                  strict->info().execution.recognition.actual_provider_chain ==
                      std::vector<std::string>{"CoreML(MLGPU)"},
              "Strict Apple profile did not select full GPU routing");
      require_hello(strict.get(), pixels_path, "gpu");
    } else {
      bool strict_rejected = false;
      try {
        auto strict = create_engine(bundle_path, CpuPartition::forbid,
                                    SessionFallback::error);
      } catch (const std::exception& exception) {
        strict_rejected =
            std::string(exception.what()).find("Intel Mac requires") !=
            std::string::npos;
      }
      require(strict_rejected,
              "Intel Mac unexpectedly accepted the strict GPU-only profile");
    }
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
