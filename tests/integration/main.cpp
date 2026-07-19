#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "common/bundle_files.hpp"
#include "core/engine_factory.hpp"
#include "inference/onnxruntime/backend.hpp"
#include "light_ocr/core.hpp"

int main() {
  try {
    Ort::SessionOptions webgpu_options;
    light_ocr::internal::add_webgpu_session_config_entries(webgpu_options);
    const std::vector<std::pair<const char*, const char*>> expected_webgpu_config{
#if defined(_WIN32)
        {"ep.webgpuexecutionprovider.dawnBackendType", "D3D12"},
#else
        {"ep.webgpuexecutionprovider.dawnBackendType", "Vulkan"},
#endif
        {"ep.webgpuexecutionprovider.preferredLayout", "NHWC"},
        {"ep.webgpuexecutionprovider.enableGraphCapture", "0"},
        {"ep.webgpuexecutionprovider.validationMode", "basic"},
        {"ep.webgpuexecutionprovider.powerPreference", "high-performance"},
    };
    for (const auto& entry : expected_webgpu_config) {
      if (!webgpu_options.HasConfigEntry(entry.first) ||
          webgpu_options.GetConfigEntry(entry.first) != entry.second) {
        std::cerr << "WebGPU session config entry mismatch: " << entry.first << '\n';
        return 1;
      }
    }
  } catch (const std::exception& exception) {
    std::cerr << "failed to inspect WebGPU session config: " << exception.what() << '\n';
    return 1;
  }

  const char* bundle_path = std::getenv("LIGHT_OCR_MODEL_BUNDLE");
  if (bundle_path == nullptr || bundle_path[0] == '\0') {
    std::cout << "SKIP LIGHT_OCR_MODEL_BUNDLE is not set\n";
    return 77;
  }
  try {
    const auto bundle_files = light_ocr::tools::load_bundle_directory(bundle_path);
    const auto detection_model = std::find_if(
        bundle_files.begin(), bundle_files.end(), [](const light_ocr::BundleFile& file) {
          return file.path == "det/inference.onnx";
        });
    if (detection_model == bundle_files.end()) {
      std::cerr << "bundle does not contain det/inference.onnx\n";
      return 1;
    }

    const auto corrupt_model = std::make_shared<const std::vector<std::uint8_t>>(
        std::initializer_list<std::uint8_t>{1, 2, 3});
    light_ocr::internal::InferenceSessionConfig detection_config;
    detection_config.model_id = "integration-detection";
    detection_config.model_sha256 = std::string(64, '0');
    detection_config.shape_policy = "dynamic";
    detection_config.qualification_id = "integration-cpu-v1";
    auto recognition_config = detection_config;
    recognition_config.model_id = "integration-recognition";
    auto corrupt_session = light_ocr::internal::OnnxSession::create(
        corrupt_model, detection_config, light_ocr::internal::ModelKind::detection);
    if (corrupt_session ||
        corrupt_session.error().code != light_ocr::ErrorCode::runtime_initialization_failed) {
      std::cerr << "corrupt ONNX did not return runtime_initialization_failed\n";
      return 1;
    }

    auto wrong_contract = light_ocr::internal::OnnxSession::create(
        detection_model->bytes, recognition_config,
        light_ocr::internal::ModelKind::recognition, 1);
    if (wrong_contract ||
        wrong_contract.error().code != light_ocr::ErrorCode::unsupported_model) {
      std::cerr << "incompatible model contract did not return unsupported_model\n";
      return 1;
    }

    auto detection_session = light_ocr::internal::OnnxSession::create(
        detection_model->bytes, detection_config,
        light_ocr::internal::ModelKind::detection);
    if (!detection_session) {
      std::cerr << "failed to create detection session for tensor boundary tests\n";
      return 1;
    }
    auto mismatched_size = detection_session.value()->run({1.0f}, {1, 3, 32, 32});
    if (mismatched_size ||
        mismatched_size.error().code != light_ocr::ErrorCode::inference_failed) {
      std::cerr << "mismatched tensor size did not return inference_failed\n";
      return 1;
    }
    auto runtime_shape_error = detection_session.value()->run(
        std::vector<float>(1U * 2U * 32U * 32U), {1, 2, 32, 32});
    if (runtime_shape_error ||
        runtime_shape_error.error().code != light_ocr::ErrorCode::inference_failed) {
      std::cerr << "runtime tensor shape rejection did not return inference_failed\n";
      return 1;
    }

    auto bundle = light_ocr::ModelBundle::create(bundle_files);
    if (!bundle) {
      std::cerr << "bundle: " << light_ocr::to_string(bundle.error().code) << ": "
                << bundle.error().message << ": " << bundle.error().detail << '\n';
      return 1;
    }
    const auto builtin_policy = light_ocr::internal::builtin_runtime_policy();
    const bool webgpu_runtime =
        std::find(builtin_policy.available_providers.begin(),
                  builtin_policy.available_providers.end(), "webgpu") !=
        builtin_policy.available_providers.end();
    light_ocr::EngineOptions integration_options;
    if (webgpu_runtime) {
      // The ordinary integration suite is hardware-independent. Direct C++
      // Auto is exercised by the real-device WebGPU qualification runner.
      integration_options.execution.provider = light_ocr::ExecutionProvider::cpu;
    }
    auto engine = light_ocr::Engine::create(std::move(bundle).value(),
                                            integration_options);
    if (!engine) {
      std::cerr << "engine: " << light_ocr::to_string(engine.error().code) << ": "
                << engine.error().message << ": " << engine.error().detail << '\n';
      return 1;
    }
    if (engine.value()->info().detection_strategy !=
            light_ocr::DetectionStrategy::bounded ||
        engine.value()->info().detection_max_side != 960 ||
        engine.value()->info().default_recognition_batch_size != 1) {
      std::cerr << "product bundle did not select bounded/960 and recognition batch 1\n";
      return 1;
    }
    const auto& execution = engine.value()->info().execution;
    const bool cpu_capability_valid =
        !execution.provider_capabilities.empty() &&
        execution.provider_capabilities[0].provider == "cpu" &&
        execution.provider_capabilities[0].package_included &&
        execution.provider_capabilities[0].device_available &&
        execution.provider_capabilities[0].device_validated;
    const bool provider_capabilities_valid =
        cpu_capability_valid &&
        (webgpu_runtime
             ? execution.provider_capabilities.size() == 2 &&
                   execution.provider_capabilities[1].provider == "webgpu" &&
                   execution.provider_capabilities[1].package_included &&
                   !execution.provider_capabilities[1].device_available &&
                   !execution.provider_capabilities[1].device_validated
             : execution.provider_capabilities.size() == 1);
    const auto expected_requested =
        webgpu_runtime ? light_ocr::ExecutionProvider::cpu
                       : light_ocr::ExecutionProvider::automatic;
    const auto expected_policy_id =
        webgpu_runtime ? std::optional<std::string>{}
                       : std::optional<std::string>{"builtin-cpu-v1"};
    const auto expected_policy_version =
        webgpu_runtime ? std::optional<std::uint32_t>{}
                       : std::optional<std::uint32_t>{1};
    if (execution.requested_provider != expected_requested ||
        execution.selection_trace.requested_provider !=
            (webgpu_runtime ? "cpu" : "auto") ||
        execution.selection_trace.policy_id != expected_policy_id ||
        execution.selection_trace.policy_version !=
            expected_policy_version ||
        execution.selection_trace.ordered_candidates !=
            std::vector<std::string>{"cpu"} ||
        execution.selection_trace.attempts.size() != 1 ||
        execution.selection_trace.attempts.front().provider != "cpu" ||
        execution.selection_trace.attempts.front().status !=
            light_ocr::CreationAttemptStatus::selected ||
        execution.selection_trace.selected_provider !=
            std::optional<std::string>{"cpu"} ||
        execution.session_fallback != light_ocr::SessionFallback::error ||
        execution.cpu_partition != light_ocr::CpuPartition::allow ||
        execution.performance_hint != light_ocr::PerformanceHint::latency ||
        execution.requested_precision != light_ocr::Precision::automatic ||
        !provider_capabilities_valid ||
        execution.detection.actual_provider_chain !=
            std::vector<std::string>{"CPUExecutionProvider"} ||
        execution.recognition.actual_provider_chain !=
            std::vector<std::string>{"CPUExecutionProvider"} ||
        execution.detection.model_id != "PP-OCRv6_small_det_onnx" ||
        execution.recognition.model_id != "PP-OCRv6_small_rec_onnx" ||
        execution.detection.model_sha256.size() != 64 ||
        execution.recognition.model_sha256.size() != 64 ||
        execution.detection.qualification_id != "builtin-cpu-v1" ||
        execution.recognition.qualification_id != "builtin-cpu-v1" ||
        !execution.detection.device_validated ||
        !execution.recognition.device_validated ||
        execution.detection.precision != "fp32" ||
        execution.recognition.shape_policy != "dynamic" ||
        execution.detection.session_fallback ||
        execution.detection.fallback_reason.has_value()) {
      std::cerr << "hardware-independent CPU execution summary is invalid\n";
      return 1;
    }
    const std::uint8_t tiny_pixel = 255;
    const light_ocr::ImageView tiny_image{&tiny_pixel, 1, 1, 1, 1,
                                          light_ocr::PixelFormat::gray8};
    auto tiny_result = engine.value()->recognize(tiny_image);
    if (!tiny_result || !tiny_result.value().lines.empty()) {
      std::cerr << "minimum 1x1 gray8 input contract did not succeed without text\n";
      return 1;
    }
    constexpr std::uint32_t width = 128;
    constexpr std::uint32_t height = 128;
    std::vector<std::uint8_t> pixels(width * height * 3, 255);
    light_ocr::ImageView image{pixels.data(), pixels.size(), width, height, width * 3,
                               light_ocr::PixelFormat::bgr8};
    auto result = engine.value()->recognize(image, light_ocr::RecognizeOptions{});
    if (!result) {
      std::cerr << "recognize: " << light_ocr::to_string(result.error().code) << ": "
                << result.error().message << ": " << result.error().detail << '\n';
      return 1;
    }
    if (result.value().image_width != width || result.value().image_height != height ||
        result.value().model_bundle_id.empty()) {
      std::cerr << "result metadata is invalid\n";
      return 1;
    }

    cv::Mat text_image(180, 800, CV_8UC3, cv::Scalar(255, 255, 255));
    cv::putText(text_image, "HELLO 123", cv::Point(35, 125), cv::FONT_HERSHEY_SIMPLEX, 2.5,
                cv::Scalar(0, 0, 0), 5, cv::LINE_AA);
    light_ocr::ImageView text_view{
        text_image.data, text_image.total() * text_image.elemSize(),
        static_cast<std::uint32_t>(text_image.cols), static_cast<std::uint32_t>(text_image.rows),
        text_image.step, light_ocr::PixelFormat::bgr8};
    auto text_result = engine.value()->recognize(
        text_view, light_ocr::RecognizeOptions{std::nullopt, 1, true, false});
    if (!text_result) {
      std::cerr << "text recognize: " << light_ocr::to_string(text_result.error().code) << ": "
                << text_result.error().message << ": " << text_result.error().detail << '\n';
      return 1;
    }
    if (text_result.value().lines.empty()) {
      std::cerr << "generated text image produced no OCR lines\n";
      return 1;
    }
    if (text_result.value().lines.size() != 1 ||
        text_result.value().lines.front().text != "HELLO 123") {
      std::cerr << "generated text image did not produce the exact golden result\n";
      return 1;
    }
    const auto& golden_line = text_result.value().lines.front();
    if (golden_line.confidence < 0 || golden_line.confidence > 1 ||
        text_result.value().timing.total_us == 0) {
      std::cerr << "result confidence or timing is invalid\n";
      return 1;
    }
    for (const auto& point : golden_line.box.points) {
      if (point.x < 0 || point.x > text_view.width || point.y < 0 ||
          point.y > text_view.height) {
        std::cerr << "result box is outside the original image coordinate contract\n";
        return 1;
      }
    }
    auto filtered = engine.value()->recognize(
        text_view, light_ocr::RecognizeOptions{1.0f, 1, true, false});
    if (!filtered || !filtered.value().lines.empty() || !filtered.value().diagnostics ||
        filtered.value().diagnostics->rejected_lines.size() != 1 ||
        filtered.value().diagnostics->rejected_lines.front().reason !=
            light_ocr::RejectionReason::below_score_threshold) {
      std::cerr << "score filtering and rejected-line diagnostics are invalid\n";
      return 1;
    }
    auto orientation = engine.value()->recognize(
        image, light_ocr::RecognizeOptions{std::nullopt, std::nullopt, false, true});
    if (orientation ||
        orientation.error().code != light_ocr::ErrorCode::unsupported_capability) {
      std::cerr << "unsupported orientation did not return unsupported_capability\n";
      return 1;
    }
    std::cout << "TEXT " << text_result.value().lines.front().text << "\n";

    cv::Mat long_running_image(1500, 1500, CV_8UC3, cv::Scalar(255, 255, 255));
    light_ocr::ImageView long_running_view{
        long_running_image.data, long_running_image.total() * long_running_image.elemSize(),
        static_cast<std::uint32_t>(long_running_image.cols),
        static_cast<std::uint32_t>(long_running_image.rows), long_running_image.step,
        light_ocr::PixelFormat::bgr8};
    std::atomic<bool> worker_started{false};
    std::atomic<bool> worker_finished{false};
    std::optional<light_ocr::Result<light_ocr::OcrResult>> worker_result;
    std::thread worker([&] {
      worker_started.store(true, std::memory_order_release);
      while (true) {
        auto attempt = engine.value()->recognize(long_running_view);
        if (!attempt &&
            attempt.error().code == light_ocr::ErrorCode::resource_limit_exceeded) {
          std::this_thread::yield();
          continue;
        }
        worker_result.emplace(std::move(attempt));
        break;
      }
      worker_finished.store(true, std::memory_order_release);
    });
    while (!worker_started.load(std::memory_order_acquire)) std::this_thread::yield();
    bool observed_busy = false;
    const auto busy_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < busy_deadline) {
      auto probe = engine.value()->recognize(image);
      if (!probe && probe.error().code == light_ocr::ErrorCode::resource_limit_exceeded) {
        observed_busy = true;
        break;
      }
      if (!probe) {
        std::cerr << "unexpected error while probing single-engine admission\n";
        worker.join();
        return 1;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!observed_busy) {
      std::cerr << "single-engine busy admission was not observed\n";
      worker.join();
      return 1;
    }
    engine.value()->close();
    if (!worker_finished.load(std::memory_order_acquire)) {
      std::cerr << "close returned before the admitted recognition call completed\n";
      worker.join();
      return 1;
    }
    worker.join();
    if (!worker_result || !*worker_result) {
      std::cerr << "admitted recognition failed during close\n";
      return 1;
    }
    engine.value()->close();
    auto after_close = engine.value()->recognize(image);
    if (after_close || after_close.error().code != light_ocr::ErrorCode::invalid_engine) {
      std::cerr << "closed engine did not return invalid_engine\n";
      return 1;
    }
    if (engine.value()->info().model_bundle_id.empty()) {
      std::cerr << "immutable engine info was lost after close\n";
      return 1;
    }

    auto make_engine = [&bundle_files, &integration_options]() {
      auto next_bundle = light_ocr::ModelBundle::create(bundle_files);
      if (!next_bundle) {
        return light_ocr::Result<std::unique_ptr<light_ocr::Engine>>::failure(
            next_bundle.error());
      }
      return light_ocr::Engine::create(std::move(next_bundle).value(),
                                       integration_options);
    };
    auto first = make_engine();
    auto second = make_engine();
    if (!first || !second) {
      std::cerr << "failed to create engines for multi-engine concurrency\n";
      return 1;
    }
    std::optional<light_ocr::Result<light_ocr::OcrResult>> first_result;
    std::optional<light_ocr::Result<light_ocr::OcrResult>> second_result;
    std::atomic<bool> release{false};
    std::thread first_thread([&] {
      while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
      first_result.emplace(first.value()->recognize(image));
    });
    std::thread second_thread([&] {
      while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
      second_result.emplace(second.value()->recognize(image));
    });
    release.store(true, std::memory_order_release);
    first_thread.join();
    second_thread.join();
    if (!first_result || !*first_result || !second_result || !*second_result) {
      std::cerr << "different engines did not recognize concurrently\n";
      return 1;
    }

    auto invalid_bundle = light_ocr::ModelBundle::create(bundle_files);
    light_ocr::EngineOptions invalid_options = integration_options;
    invalid_options.intra_op_threads = 0;
    auto invalid_engine = light_ocr::Engine::create(
        std::move(invalid_bundle).value(), invalid_options);
    if (invalid_engine || invalid_engine.error().code != light_ocr::ErrorCode::invalid_argument) {
      std::cerr << "invalid engine options did not return invalid_argument\n";
      return 1;
    }

    auto invalid_execution_bundle = light_ocr::ModelBundle::create(bundle_files);
    light_ocr::EngineOptions invalid_execution_options = integration_options;
    invalid_execution_options.execution.device_id = 0;
    auto invalid_execution_engine = light_ocr::Engine::create(
        std::move(invalid_execution_bundle).value(), invalid_execution_options);
    if (invalid_execution_engine ||
        invalid_execution_engine.error().code !=
            light_ocr::ErrorCode::invalid_argument) {
      std::cerr << "invalid CPU execution options did not return invalid_argument\n";
      return 1;
    }

    auto exact_bundle = light_ocr::ModelBundle::create(bundle_files);
    light_ocr::EngineOptions exact_options = integration_options;
    exact_options.detection.strategy = light_ocr::DetectionStrategy::upstream_exact;
    exact_options.recognition_batch_size = 8;
    auto exact_engine = light_ocr::Engine::create(
        std::move(exact_bundle).value(), exact_options);
    if (!exact_engine ||
        exact_engine.value()->info().detection_strategy !=
            light_ocr::DetectionStrategy::upstream_exact ||
        exact_engine.value()->info().detection_max_side != 4000 ||
        exact_engine.value()->info().default_recognition_batch_size != 8) {
      std::cerr << "explicit upstream-exact profile did not preserve source limits\n";
      return 1;
    }
    exact_engine.value()->close();

    auto tiled_bundle = light_ocr::ModelBundle::create(bundle_files);
    light_ocr::EngineOptions tiled_options = integration_options;
    tiled_options.detection.strategy = light_ocr::DetectionStrategy::tiled;
    auto tiled_engine = light_ocr::Engine::create(
        std::move(tiled_bundle).value(), tiled_options);
    if (!tiled_engine ||
        tiled_engine.value()->info().detection_strategy !=
            light_ocr::DetectionStrategy::tiled ||
        tiled_engine.value()->info().detection_max_side != 1280 ||
        tiled_engine.value()->info().normalized_config_schema_version != "1.2" ||
        !tiled_engine.value()->info().capabilities.tiled_detection ||
        !tiled_engine.value()->info().tiled_detection ||
        tiled_engine.value()->info().tiled_detection->contract_version !=
            "tiled-v1") {
      std::cerr << "explicit tiled profile did not expose its runtime contract\n";
      return 1;
    }
    cv::Mat tiled_blank(2048, 2048, CV_8UC3, cv::Scalar(255, 255, 255));
    light_ocr::ImageView tiled_blank_view{
        tiled_blank.data, tiled_blank.total() * tiled_blank.elemSize(), 2048,
        2048, tiled_blank.step, light_ocr::PixelFormat::bgr8};
    light_ocr::RecognizeOptions tiled_recognize_options;
    tiled_recognize_options.include_diagnostics = true;
    auto tiled_result = tiled_engine.value()->recognize(
        tiled_blank_view, tiled_recognize_options);
    if (!tiled_result || !tiled_result.value().lines.empty() ||
        !tiled_result.value().diagnostics ||
        tiled_result.value().diagnostics->detection_passes.size() != 4 ||
        tiled_result.value().diagnostics->detection_input_width != 1280 ||
        tiled_result.value().diagnostics->detection_input_height != 1280 ||
        tiled_result.value().diagnostics->raw_detection_boxes != 0 ||
        tiled_result.value().diagnostics->suppressed_duplicate_boxes != 0 ||
        tiled_result.value().diagnostics->max_live_detection_pass_buffers != 1) {
      std::cerr << "2048 tiled blank contract or diagnostics are invalid\n";
      return 1;
    }
    if (tiled_result.value().diagnostics->detection_passes[1].x != 768 ||
        tiled_result.value().diagnostics->detection_passes[2].y != 768) {
      std::cerr << "2048 tiled plan is not the locked row-major plan\n";
      return 1;
    }
    cv::Mat boundary_text(2048, 2048, CV_8UC3, cv::Scalar(255, 255, 255));
    cv::putText(boundary_text, "HELLO 123", cv::Point(1050, 600),
                cv::FONT_HERSHEY_SIMPLEX, 2.5, cv::Scalar(0, 0, 0), 5,
                cv::LINE_AA);
    light_ocr::ImageView boundary_text_view{
        boundary_text.data, boundary_text.total() * boundary_text.elemSize(),
        2048, 2048, boundary_text.step, light_ocr::PixelFormat::bgr8};
    auto boundary_result = tiled_engine.value()->recognize(
        boundary_text_view, tiled_recognize_options);
    if (!boundary_result || boundary_result.value().lines.size() != 1 ||
        boundary_result.value().lines.front().text != "HELLO 123" ||
        !boundary_result.value().diagnostics ||
        boundary_result.value().diagnostics->raw_detection_boxes < 2 ||
        boundary_result.value().diagnostics->suppressed_duplicate_boxes < 1 ||
        boundary_result.value().diagnostics->raw_detection_boxes -
                boundary_result.value().diagnostics->suppressed_duplicate_boxes !=
            boundary_result.value().diagnostics->accepted_boxes) {
      std::cerr << "tiled boundary text was not recognized and deduplicated once\n";
      return 1;
    }
    auto boundary_without_diagnostics =
        tiled_engine.value()->recognize(boundary_text_view);
    bool same_boundary_box = boundary_without_diagnostics &&
                             !boundary_without_diagnostics.value().lines.empty();
    if (same_boundary_box) {
      for (std::size_t index = 0; index < 4; ++index) {
        const auto& left = boundary_without_diagnostics.value()
                               .lines.front()
                               .box.points[index];
        const auto& right =
            boundary_result.value().lines.front().box.points[index];
        same_boundary_box = same_boundary_box && left.x == right.x &&
                            left.y == right.y;
      }
    }
    if (!boundary_without_diagnostics ||
        boundary_without_diagnostics.value().diagnostics ||
        boundary_without_diagnostics.value().lines.size() != 1 ||
        boundary_without_diagnostics.value().lines.front().text !=
            boundary_result.value().lines.front().text ||
        !same_boundary_box) {
      std::cerr << "tiled diagnostics changed the OCR result\n";
      return 1;
    }
    light_ocr::RecognizeOptions tiled_side_override;
    tiled_side_override.detection_max_side = 960;
    auto tiled_override = tiled_engine.value()->recognize(
        image, tiled_side_override);
    if (tiled_override ||
        tiled_override.error().code != light_ocr::ErrorCode::invalid_argument) {
      std::cerr << "tiled request side override was not rejected\n";
      return 1;
    }
    tiled_engine.value()->close();

    auto tile_limited_bundle = light_ocr::ModelBundle::create(bundle_files);
    light_ocr::EngineOptions tile_limited_options = tiled_options;
    tile_limited_options.reduced_limits = light_ocr::ResourceLimits{};
    tile_limited_options.reduced_limits->max_detection_tiles = 3;
    auto tile_limited_engine = light_ocr::Engine::create(
        std::move(tile_limited_bundle).value(), tile_limited_options);
    auto tile_limited_result = tile_limited_engine.value()->recognize(
        tiled_blank_view);
    if (tile_limited_result ||
        tile_limited_result.error().code !=
            light_ocr::ErrorCode::resource_limit_exceeded) {
      std::cerr << "tiled tile ceiling was not enforced before inference\n";
      return 1;
    }
    tile_limited_engine.value()->close();

    auto candidate_limited_bundle = light_ocr::ModelBundle::create(bundle_files);
    light_ocr::EngineOptions candidate_limited_options = tiled_options;
    candidate_limited_options.reduced_limits = light_ocr::ResourceLimits{};
    candidate_limited_options.reduced_limits->max_detection_candidates = 1;
    auto candidate_limited_engine = light_ocr::Engine::create(
        std::move(candidate_limited_bundle).value(), candidate_limited_options);
    auto candidate_limited_result = candidate_limited_engine.value()->recognize(
        boundary_text_view);
    if (candidate_limited_result ||
        candidate_limited_result.error().code !=
            light_ocr::ErrorCode::resource_limit_exceeded) {
      std::cerr << "tiled global candidate ceiling returned a partial result\n";
      return 1;
    }
    candidate_limited_engine.value()->close();

    std::cout << "PASS real PP-OCRv6 bounded, exact, tiled, concurrency, and lifecycle\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
