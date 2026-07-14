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
#include "inference/onnxruntime/backend.hpp"
#include "light_ocr/core.hpp"

int main() {
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
    auto corrupt_session = light_ocr::internal::OnnxSession::create(
        corrupt_model, 1, 1, light_ocr::internal::ModelKind::detection);
    if (corrupt_session ||
        corrupt_session.error().code != light_ocr::ErrorCode::runtime_initialization_failed) {
      std::cerr << "corrupt ONNX did not return runtime_initialization_failed\n";
      return 1;
    }

    auto wrong_contract = light_ocr::internal::OnnxSession::create(
        detection_model->bytes, 1, 1, light_ocr::internal::ModelKind::recognition, 1);
    if (wrong_contract ||
        wrong_contract.error().code != light_ocr::ErrorCode::unsupported_model) {
      std::cerr << "incompatible model contract did not return unsupported_model\n";
      return 1;
    }

    auto detection_session = light_ocr::internal::OnnxSession::create(
        detection_model->bytes, 1, 1, light_ocr::internal::ModelKind::detection);
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
    auto engine = light_ocr::Engine::create(std::move(bundle).value());
    if (!engine) {
      std::cerr << "engine: " << light_ocr::to_string(engine.error().code) << ": "
                << engine.error().message << ": " << engine.error().detail << '\n';
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

    auto make_engine = [&bundle_files]() {
      auto next_bundle = light_ocr::ModelBundle::create(bundle_files);
      if (!next_bundle) {
        return light_ocr::Result<std::unique_ptr<light_ocr::Engine>>::failure(
            next_bundle.error());
      }
      return light_ocr::Engine::create(std::move(next_bundle).value());
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
    light_ocr::EngineOptions invalid_options;
    invalid_options.intra_op_threads = 0;
    auto invalid_engine = light_ocr::Engine::create(
        std::move(invalid_bundle).value(), invalid_options);
    if (invalid_engine || invalid_engine.error().code != light_ocr::ErrorCode::invalid_argument) {
      std::cerr << "invalid engine options did not return invalid_argument\n";
      return 1;
    }

    std::cout << "PASS real PP-OCRv6 golden result, limits, concurrency, and lifecycle\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
