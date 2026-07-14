#include "light_ocr/core.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "detection/db_postprocess.hpp"
#include "geometry/geometry.hpp"
#include "inference/onnxruntime/backend.hpp"
#include "model/bundle_data.hpp"
#include "preprocess/image.hpp"
#include "preprocess/tensor.hpp"
#include "recognition/ctc_decode.hpp"
#include "result/assemble.hpp"
#include "util/checked_math.hpp"

#ifndef LIGHT_OCR_VERSION
#define LIGHT_OCR_VERSION "0.0.0"
#endif

namespace light_ocr {
namespace {

using Clock = std::chrono::steady_clock;

std::uint64_t elapsed_us(Clock::time_point begin, Clock::time_point end) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count());
}

template <class T>
Result<T> failure(ErrorCode code, const char* message, std::string detail = {}) {
  return Result<T>::failure(Error{code, message, std::move(detail)});
}

bool valid_score(float value) { return std::isfinite(value) && value >= 0 && value <= 1; }

bool valid_limits(const ResourceLimits& value, const ResourceLimits& ceiling) {
  return value.max_width > 0 && value.max_width <= ceiling.max_width &&
         value.max_height > 0 && value.max_height <= ceiling.max_height &&
         value.max_pixels > 0 && value.max_pixels <= ceiling.max_pixels &&
         value.max_detection_side > 0 &&
         value.max_detection_side <= ceiling.max_detection_side &&
         value.max_detection_candidates > 0 &&
         value.max_detection_candidates <= ceiling.max_detection_candidates &&
         value.max_recognition_batch_size > 0 &&
         value.max_recognition_batch_size <= ceiling.max_recognition_batch_size &&
         value.max_recognition_width > 0 &&
         value.max_recognition_width <= ceiling.max_recognition_width &&
         value.max_temporary_bytes > 0 &&
         value.max_temporary_bytes <= ceiling.max_temporary_bytes &&
         value.max_concurrent_calls == 1;
}

class EngineImpl final : public Engine {
 public:
  EngineImpl(std::shared_ptr<const internal::BundleData> bundle,
             std::unique_ptr<internal::OnnxSession> detection,
             std::unique_ptr<internal::OnnxSession> recognition, EngineInfo info)
      : bundle_(std::move(bundle)),
        detection_(std::move(detection)),
        recognition_(std::move(recognition)),
        info_(std::move(info)) {}

  ~EngineImpl() noexcept override { close(); }

  Result<OcrResult> recognize(const ImageView& image,
                              const RecognizeOptions& options) noexcept override {
    try {
      {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (closing_) return failure<OcrResult>(ErrorCode::invalid_engine, "Engine is closed");
        if (active_) {
          return failure<OcrResult>(ErrorCode::resource_limit_exceeded,
                                    "Engine already has an active recognition call");
        }
        active_ = true;
      }
      struct AdmissionGuard {
        EngineImpl* engine;
        ~AdmissionGuard() noexcept {
          try {
            std::lock_guard<std::mutex> lock(engine->state_mutex_);
            engine->active_ = false;
            engine->state_changed_.notify_all();
          } catch (...) {
            // No exception may cross the recognize noexcept boundary. A live standard mutex and
            // condition variable are not expected to fail during request cleanup.
          }
        }
      } guard{this};

      const auto total_begin = Clock::now();
      if (options.use_textline_orientation) {
        return failure<OcrResult>(ErrorCode::unsupported_capability,
                                  "Text-line orientation is not available in this bundle");
      }
      const float score_threshold = options.recognition_score_threshold.value_or(
          info_.default_recognition_score_threshold);
      const auto batch_size = options.recognition_batch_size.value_or(
          info_.default_recognition_batch_size);
      if (!valid_score(score_threshold) || batch_size == 0 ||
          batch_size > info_.limits.max_recognition_batch_size) {
        return failure<OcrResult>(ErrorCode::invalid_argument,
                                  "Recognition options are outside effective limits");
      }

      Timing timing;
      auto stage_begin = Clock::now();
      auto validated_result = internal::validate_and_convert_image(image, info_.limits);
      auto stage_end = Clock::now();
      timing.input_validation_us = elapsed_us(stage_begin, stage_end);
      if (!validated_result) return Result<OcrResult>::failure(validated_result.error());
      auto validated = std::move(validated_result).value();
      std::uint64_t image_bytes = 0;
      if (!internal::checked_mul<std::uint64_t>(validated.bgr.total(),
                                                validated.bgr.elemSize(), &image_bytes) ||
          image_bytes > info_.limits.max_temporary_bytes) {
        return failure<OcrResult>(ErrorCode::resource_limit_exceeded,
                                  "Converted image exceeds the request memory budget");
      }

      internal::DetectionBoxes detected;
      {
        stage_begin = Clock::now();
        auto detection_limits = info_.limits;
        detection_limits.max_temporary_bytes -= image_bytes;
        auto detection_input_result = internal::make_detection_input(
            validated.bgr, bundle_->detection, detection_limits);
        stage_end = Clock::now();
        timing.detection_preprocess_us = elapsed_us(stage_begin, stage_end);
        if (!detection_input_result) {
          return Result<OcrResult>::failure(detection_input_result.error());
        }
        auto detection_input = std::move(detection_input_result).value();

        stage_begin = Clock::now();
        auto detection_output_result =
            detection_->run(detection_input.values, detection_input.shape);
        stage_end = Clock::now();
        timing.detection_inference_us = elapsed_us(stage_begin, stage_end);
        if (!detection_output_result) {
          return Result<OcrResult>::failure(detection_output_result.error());
        }
        auto detection_output = std::move(detection_output_result).value();

        stage_begin = Clock::now();
        auto detected_result = internal::db_postprocess(
            detection_output.values.data(), detection_output.values.size(),
            detection_output.shape, image.width, image.height, bundle_->detection,
            info_.limits);
        stage_end = Clock::now();
        timing.detection_postprocess_us = elapsed_us(stage_begin, stage_end);
        if (!detected_result) return Result<OcrResult>::failure(detected_result.error());
        detected = std::move(detected_result).value();
      }

      stage_begin = Clock::now();
      auto sorted_boxes =
          internal::sort_reading_order(std::move(detected.boxes), bundle_->geometry);
      auto crop_limits = info_.limits;
      crop_limits.max_temporary_bytes -= image_bytes;
      auto crops_result = internal::crop_text_regions(
          validated.bgr, sorted_boxes, bundle_->geometry, crop_limits);
      stage_end = Clock::now();
      timing.crop_and_sort_us = elapsed_us(stage_begin, stage_end);
      if (!crops_result) return Result<OcrResult>::failure(crops_result.error());
      auto crops = std::move(crops_result).value();
      std::uint64_t crop_bytes = 0;
      for (const auto& crop : crops) {
        std::uint64_t bytes = 0;
        if (!internal::checked_mul<std::uint64_t>(crop.total(), crop.elemSize(), &bytes) ||
            !internal::checked_add<std::uint64_t>(crop_bytes, bytes, &crop_bytes) ||
            crop_bytes > info_.limits.max_temporary_bytes) {
          return failure<OcrResult>(ErrorCode::resource_limit_exceeded,
                                    "Text crops exceed the request memory budget");
        }
      }
      validated.bgr.release();

      std::vector<internal::DecodedText> decoded(sorted_boxes.size());
      if (!crops.empty()) {
        stage_begin = Clock::now();
        auto recognition_limits = info_.limits;
        recognition_limits.max_temporary_bytes -= crop_bytes;
        auto batches_result = internal::make_recognition_batches(
            crops, bundle_->recognition, batch_size, recognition_limits);
        stage_end = Clock::now();
        timing.recognition_preprocess_us = elapsed_us(stage_begin, stage_end);
        if (!batches_result) return Result<OcrResult>::failure(batches_result.error());
        auto batches = std::move(batches_result).value();
        std::vector<cv::Mat>().swap(crops);

        for (auto& batch : batches) {
          stage_begin = Clock::now();
          auto output_result = recognition_->run(batch.values, batch.shape);
          stage_end = Clock::now();
          timing.recognition_inference_us += elapsed_us(stage_begin, stage_end);
          if (!output_result) return Result<OcrResult>::failure(output_result.error());
          auto output = std::move(output_result).value();
          std::vector<float>().swap(batch.values);

          stage_begin = Clock::now();
          auto batch_decoded_result = internal::decode_ctc(
              output.values.data(), output.values.size(), output.shape,
              bundle_->recognition.characters, bundle_->recognition.blank_index,
              bundle_->recognition.collapse_repeats);
          stage_end = Clock::now();
          timing.recognition_postprocess_us += elapsed_us(stage_begin, stage_end);
          if (!batch_decoded_result) {
            return Result<OcrResult>::failure(batch_decoded_result.error());
          }
          auto batch_decoded = std::move(batch_decoded_result).value();
          if (batch_decoded.size() != batch.input_indices.size()) {
            return failure<OcrResult>(ErrorCode::postprocess_failed,
                                      "Recognition batch result count is invalid");
          }
          for (std::size_t index = 0; index < batch.input_indices.size(); ++index) {
            decoded[batch.input_indices[index]] = std::move(batch_decoded[index]);
          }
        }
      }

      auto assembled = internal::assemble_ocr_result(
          image.width, image.height, bundle_->id, timing,
          detected.contour_candidates, std::move(sorted_boxes), std::move(decoded),
          score_threshold, options.include_diagnostics);
      if (!assembled) return Result<OcrResult>::failure(assembled.error());
      auto result = std::move(assembled).value();
      result.timing.total_us = elapsed_us(total_begin, Clock::now());
      return Result<OcrResult>::success(std::move(result));
    } catch (const std::exception& exception) {
      return failure<OcrResult>(ErrorCode::internal_error, "Unexpected recognition failure",
                                exception.what());
    } catch (...) {
      return failure<OcrResult>(ErrorCode::internal_error, "Unknown recognition failure");
    }
  }

  const EngineInfo& info() const noexcept override { return info_; }

  void close() noexcept override {
    try {
      std::unique_lock<std::mutex> lock(state_mutex_);
      closing_ = true;
      state_changed_.wait(lock, [this] { return !active_; });
      detection_.reset();
      recognition_.reset();
      bundle_.reset();
    } catch (...) {
      // close is a noexcept safety boundary. Standard synchronization operations are not
      // expected to fail for a live object, and there is no recoverable public error channel.
    }
  }

 private:
  std::shared_ptr<const internal::BundleData> bundle_;
  std::unique_ptr<internal::OnnxSession> detection_;
  std::unique_ptr<internal::OnnxSession> recognition_;
  EngineInfo info_;
  mutable std::mutex state_mutex_;
  std::condition_variable state_changed_;
  bool active_ = false;
  bool closing_ = false;
};

}  // namespace

Engine::~Engine() noexcept = default;

Result<std::unique_ptr<Engine>> Engine::create(ModelBundle bundle,
                                               const EngineOptions& options) {
  try {
    if (!bundle.data_) {
      return failure<std::unique_ptr<Engine>>(ErrorCode::invalid_model_bundle,
                                              "Model bundle has no validated data");
    }
    if (options.intra_op_threads == 0 || options.inter_op_threads == 0) {
      return failure<std::unique_ptr<Engine>>(ErrorCode::invalid_argument,
                                              "ONNX Runtime thread counts must be positive");
    }
    auto limits = options.reduced_limits.value_or(bundle.data_->limits);
    if (!valid_limits(limits, bundle.data_->limits)) {
      return failure<std::unique_ptr<Engine>>(ErrorCode::invalid_argument,
                                              "Engine resource limits are invalid or increase bundle ceilings");
    }
    const auto score_threshold = options.recognition_score_threshold.value_or(
        bundle.data_->recognition.default_score_threshold);
    const auto batch_size = options.recognition_batch_size.value_or(
        bundle.data_->recognition.default_batch_size);
    if (!valid_score(score_threshold) || batch_size == 0 ||
        batch_size > limits.max_recognition_batch_size ||
        batch_size > bundle.data_->recognition.maximum_batch_size) {
      return failure<std::unique_ptr<Engine>>(ErrorCode::invalid_argument,
                                              "Engine recognition defaults are outside limits");
    }
    const auto& detection_bytes = bundle.data_->files.at(bundle.data_->detection_model_path);
    const auto& recognition_bytes = bundle.data_->files.at(bundle.data_->recognition_model_path);
    auto detection = internal::OnnxSession::create(
        detection_bytes, options.intra_op_threads, options.inter_op_threads,
        internal::ModelKind::detection);
    if (!detection) return Result<std::unique_ptr<Engine>>::failure(detection.error());
    auto recognition = internal::OnnxSession::create(
        recognition_bytes, options.intra_op_threads, options.inter_op_threads,
        internal::ModelKind::recognition, bundle.data_->recognition.characters.size() + 1);
    if (!recognition) return Result<std::unique_ptr<Engine>>::failure(recognition.error());

    EngineInfo info;
    info.core_version = LIGHT_OCR_VERSION;
    info.model_bundle_id = bundle.data_->id;
    info.model_bundle_schema_version = bundle.data_->schema_version;
    info.backend = "ONNX Runtime 1.22.0";
    info.execution_provider = "CPUExecutionProvider";
    info.capabilities = bundle.data_->capabilities;
    info.limits = limits;
    info.intra_op_threads = options.intra_op_threads;
    info.inter_op_threads = options.inter_op_threads;
    info.default_recognition_score_threshold = score_threshold;
    info.default_recognition_batch_size = batch_size;
    return Result<std::unique_ptr<Engine>>::success(std::unique_ptr<Engine>(new EngineImpl(
        std::move(bundle.data_), std::move(detection).value(), std::move(recognition).value(),
        std::move(info))));
  } catch (const std::exception& exception) {
    return failure<std::unique_ptr<Engine>>(ErrorCode::runtime_initialization_failed,
                                            "Unexpected engine initialization failure",
                                            exception.what());
  } catch (...) {
    return failure<std::unique_ptr<Engine>>(ErrorCode::internal_error,
                                            "Unknown engine initialization failure");
  }
}

}  // namespace light_ocr
