#include "light_ocr/core.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "core/engine_factory.hpp"
#include "detection/db_postprocess.hpp"
#include "detection/tiled.hpp"
#include "geometry/geometry.hpp"
#include "inference/backend.hpp"
#if defined(LIGHT_OCR_HAS_COREML)
#include "inference/coreml/backend.hpp"
#endif
#include "inference/onnxruntime/backend.hpp"
#include "inference/selection.hpp"
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

std::string apple_recognition_function_name(std::uint32_t width) {
  const auto value = std::to_string(width);
  return "w" + std::string(4 - value.size(), '0') + value;
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
         value.max_detection_tiles > 0 &&
         value.max_detection_tiles <= ceiling.max_detection_tiles &&
         value.max_recognition_batch_size > 0 &&
         value.max_recognition_batch_size <= ceiling.max_recognition_batch_size &&
         value.max_recognition_width > 0 &&
         value.max_recognition_width <= ceiling.max_recognition_width &&
         value.max_temporary_bytes > 0 &&
         value.max_temporary_bytes <= ceiling.max_temporary_bytes &&
         value.max_concurrent_calls == 1;
}

bool valid_execution_options(const ExecutionOptions& options) {
  if (options.session_fallback != SessionFallback::error ||
      options.performance_hint != PerformanceHint::latency) {
    return false;
  }
  if (options.provider == ExecutionProvider::automatic) {
    return !options.device_id.has_value() &&
           options.cpu_partition == CpuPartition::allow &&
           options.precision == Precision::automatic;
  }
  if (options.provider == ExecutionProvider::cpu) {
    return !options.device_id.has_value() &&
           options.cpu_partition == CpuPartition::allow &&
           (options.precision == Precision::automatic ||
            options.precision == Precision::fp32);
  }
  if (options.provider == ExecutionProvider::webgpu) {
    return !options.device_id.has_value() &&
           (options.cpu_partition == CpuPartition::allow ||
            options.cpu_partition == CpuPartition::forbid) &&
           (options.precision == Precision::automatic ||
            options.precision == Precision::fp32);
  }
  return options.provider == ExecutionProvider::apple &&
         !options.device_id.has_value() &&
         (options.cpu_partition == CpuPartition::allow ||
          options.cpu_partition == CpuPartition::forbid) &&
         (options.precision == Precision::automatic ||
          options.precision == Precision::fp16);
}

const char* provider_name(ExecutionProvider provider) {
  switch (provider) {
    case ExecutionProvider::automatic: return "auto";
    case ExecutionProvider::cpu: return "cpu";
    case ExecutionProvider::apple: return "apple";
    case ExecutionProvider::webgpu: return "webgpu";
  }
  return "auto";
}

bool known_provider(const std::string& provider) {
  return provider == "cpu" || provider == "apple" || provider == "webgpu";
}

bool policy_includes_provider(const internal::RuntimePolicy& policy,
                              const std::string& provider) {
  return std::find(policy.available_providers.begin(),
                   policy.available_providers.end(),
                   provider) != policy.available_providers.end();
}

std::string policy_qualification_id(const internal::RuntimePolicy& policy,
                                    const std::string& provider) {
  const auto iterator = std::find(policy.available_providers.begin(),
                                  policy.available_providers.end(), provider);
  if (iterator == policy.available_providers.end() ||
      policy.provider_qualification_ids.empty()) {
    return {};
  }
  const auto index = static_cast<std::size_t>(
      std::distance(policy.available_providers.begin(), iterator));
  return policy.provider_qualification_ids[index];
}

bool valid_runtime_policy(const internal::RuntimePolicy& policy) {
  const bool valid_provider_hash = policy.webgpu_provider_sha256.empty() ||
      (policy.webgpu_provider_sha256.size() == 64 &&
       std::all_of(policy.webgpu_provider_sha256.begin(),
                   policy.webgpu_provider_sha256.end(), [](char value) {
                     return (value >= '0' && value <= '9') ||
                            (value >= 'a' && value <= 'f');
                   }));
  if (policy.id.empty() || policy.version == 0 ||
      policy.ordered_candidates.empty() ||
      policy.ordered_candidates.back() != "cpu" ||
      !policy_includes_provider(policy, "cpu") ||
      policy.qualification_only == policy.released ||
      (!policy.webgpu_provider_library.empty() &&
       !policy_includes_provider(policy, "webgpu")) ||
      ((policy.webgpu_provider_bytes != 0 ||
        !policy.webgpu_provider_sha256.empty()) &&
       (!policy_includes_provider(policy, "webgpu") ||
        policy.webgpu_provider_library.empty() ||
        policy.webgpu_provider_bytes == 0 ||
        !valid_provider_hash)) ||
      (!policy.provider_qualification_ids.empty() &&
       policy.provider_qualification_ids.size() !=
           policy.available_providers.size())) {
    return false;
  }
  for (auto iterator = policy.available_providers.begin();
       iterator != policy.available_providers.end(); ++iterator) {
    const auto index = static_cast<std::size_t>(
        std::distance(policy.available_providers.begin(), iterator));
    if (!known_provider(*iterator) ||
        (!policy.provider_qualification_ids.empty() &&
         policy.provider_qualification_ids[index].empty()) ||
        std::find(policy.available_providers.begin(), iterator, *iterator) !=
            iterator) {
      return false;
    }
  }
  for (auto iterator = policy.ordered_candidates.begin();
       iterator != policy.ordered_candidates.end(); ++iterator) {
    if (!known_provider(*iterator) ||
        !policy_includes_provider(policy, *iterator) ||
        std::find(policy.ordered_candidates.begin(), iterator, *iterator) !=
            iterator) {
      return false;
    }
  }
  return true;
}

#if defined(LIGHT_OCR_HAS_COREML)
internal::AppleModelPackage make_apple_package(
    const internal::BundleData& bundle,
    const internal::AppleModelConfig& model,
    const internal::AppleProviderConfig& provider,
    bool recognition) {
  internal::AppleModelPackage package;
  package.root_path = model.package_path;
  package.package_sha256 = model.package_sha256;
  package.input_name = model.input_name;
  package.output_name = model.output_name;
  package.qualification_id = provider.qualification_id;
  package.device_policy = provider.device_policy;
  package.architectures = provider.architectures;
  package.validated_device_families = provider.validated_device_families;
  const auto prefix = model.package_path + "/";
  for (const auto& file : bundle.files) {
    if (file.first.compare(0, prefix.size(), prefix) == 0) {
      package.files.push_back(internal::ModelPackageFile{
          file.first.substr(prefix.size()), file.second});
    }
  }
  std::sort(package.files.begin(), package.files.end(),
            [](const auto& left, const auto& right) {
              return left.path < right.path;
            });
  if (recognition) {
    package.recognition_width_multiple =
        provider.recognition_width_multiple;
    package.recognition_ane_maximum_width =
        provider.recognition_ane_maximum_width;
    package.maximum_cached_functions = provider.maximum_cached_functions;
  }
  return package;
}
#endif

struct CreatedSessions {
  ExecutionProvider provider = ExecutionProvider::cpu;
  std::unique_ptr<internal::InferenceSession> detection;
  std::unique_ptr<internal::InferenceSession> recognition;
  std::uint32_t recognition_width_multiple = 1;
  std::vector<std::uint32_t> recognition_width_buckets;
  std::uint32_t maximum_backend_batch_size = 1;
};

class EngineImpl final : public Engine {
 public:
  EngineImpl(std::shared_ptr<const internal::BundleData> bundle,
             std::unique_ptr<internal::InferenceSession> detection,
             std::unique_ptr<internal::InferenceSession> recognition,
             EngineInfo info, std::uint32_t recognition_width_multiple,
             std::vector<std::uint32_t> recognition_width_buckets,
             std::uint32_t maximum_backend_batch_size)
      : bundle_(std::move(bundle)),
        detection_(std::move(detection)),
        recognition_(std::move(recognition)),
        info_(std::move(info)),
        recognition_width_multiple_(recognition_width_multiple),
        recognition_width_buckets_(std::move(recognition_width_buckets)),
        maximum_backend_batch_size_(maximum_backend_batch_size) {}

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
      const auto detection_max_side = options.detection_max_side.value_or(
          info_.detection_max_side);
      if (!valid_score(score_threshold) || batch_size == 0 ||
          batch_size > info_.limits.max_recognition_batch_size ||
          batch_size > maximum_backend_batch_size_ ||
          detection_max_side == 0 ||
          detection_max_side > info_.detection_max_side ||
          (info_.detection_strategy != DetectionStrategy::bounded &&
           info_.detection_strategy != DetectionStrategy::tiled &&
           info_.detection_strategy != DetectionStrategy::upstream_exact) ||
          (info_.detection_strategy == DetectionStrategy::bounded &&
           detection_max_side % bundle_->detection.dimension_multiple != 0) ||
          (info_.detection_strategy == DetectionStrategy::upstream_exact &&
           options.detection_max_side.has_value()) ||
          (info_.detection_strategy == DetectionStrategy::tiled &&
           options.detection_max_side.has_value())) {
        return failure<OcrResult>(ErrorCode::invalid_argument,
                                  "Request options are outside effective limits");
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
      std::uint32_t detection_input_width = 0;
      std::uint32_t detection_input_height = 0;
      std::uint32_t raw_detection_boxes = 0;
      std::uint32_t suppressed_duplicate_boxes = 0;
      std::uint32_t max_live_detection_pass_buffers = 0;
      std::vector<DetectionPassShape> detection_passes;
      auto run_detection_pass = [&](const cv::Mat& pass_image,
                                    std::uint32_t original_width,
                                    std::uint32_t original_height,
                                    DetectionStrategy preprocess_strategy,
                                    std::uint32_t pass_max_side,
                                    const ResourceLimits& pass_limits,
                                    bool reject_candidate_overflow,
                                    DetectionPassShape* pass_shape)
          -> Result<internal::DetectionBoxes> {
        stage_begin = Clock::now();
        auto detection_input_result = internal::make_detection_input(
            pass_image, bundle_->detection, preprocess_strategy, pass_max_side,
            pass_limits);
        stage_end = Clock::now();
        timing.detection_preprocess_us += elapsed_us(stage_begin, stage_end);
        if (!detection_input_result) {
          return Result<internal::DetectionBoxes>::failure(
              detection_input_result.error());
        }
        auto detection_input = std::move(detection_input_result).value();
        const auto pass_input_height =
            static_cast<std::uint32_t>(detection_input.shape[2]);
        const auto pass_input_width =
            static_cast<std::uint32_t>(detection_input.shape[3]);
        detection_input_height = std::max(detection_input_height, pass_input_height);
        detection_input_width = std::max(detection_input_width, pass_input_width);
        pass_shape->tensor_height = pass_input_height;
        pass_shape->tensor_width = pass_input_width;

        stage_begin = Clock::now();
        auto detection_output_result =
            detection_->run(detection_input.values, detection_input.shape);
        stage_end = Clock::now();
        timing.detection_inference_us += elapsed_us(stage_begin, stage_end);
        if (!detection_output_result) {
          return Result<internal::DetectionBoxes>::failure(
              detection_output_result.error());
        }
        auto detection_output = std::move(detection_output_result).value();

        stage_begin = Clock::now();
        auto detected_result = internal::db_postprocess(
            detection_output.data(), detection_output.size(),
            detection_output.shape(), original_width, original_height,
            bundle_->detection, pass_limits, false,
            reject_candidate_overflow);
        stage_end = Clock::now();
        timing.detection_postprocess_us += elapsed_us(stage_begin, stage_end);
        if (!detected_result) {
          return Result<internal::DetectionBoxes>::failure(
              detected_result.error());
        }
        auto pass_detected = std::move(detected_result).value();
        pass_shape->contour_candidates = pass_detected.total_contours;
        if (pass_detected.boxes.size() >
            std::numeric_limits<std::uint32_t>::max()) {
          return failure<internal::DetectionBoxes>(
              ErrorCode::resource_limit_exceeded,
              "Detection box count exceeds its representable limit");
        }
        pass_shape->raw_candidates =
            static_cast<std::uint32_t>(pass_detected.boxes.size());
        return Result<internal::DetectionBoxes>::success(
            std::move(pass_detected));
      };

      auto detection_limits = info_.limits;
      detection_limits.max_temporary_bytes -= image_bytes;
      if (info_.detection_strategy == DetectionStrategy::tiled) {
        if (!bundle_->tiled_detection) {
          return failure<OcrResult>(ErrorCode::unsupported_capability,
                                    "Tiled detection is unavailable in this bundle");
        }
        auto tile_plan_result = internal::plan_detection_tiles(
            image.width, image.height, *bundle_->tiled_detection,
            info_.limits.max_detection_tiles);
        if (!tile_plan_result) {
          return Result<OcrResult>::failure(tile_plan_result.error());
        }
        auto tile_plan = std::move(tile_plan_result).value();
        std::vector<internal::TiledCandidate> raw_candidates;
        raw_candidates.reserve(std::min<std::size_t>(
            info_.limits.max_detection_candidates, 256));
        std::uint32_t total_contours = 0;
        if (options.include_diagnostics) detection_passes.reserve(tile_plan.size());
        for (const auto& tile : tile_plan) {
          auto pass_limits = detection_limits;
          pass_limits.max_detection_candidates =
              info_.limits.max_detection_candidates - total_contours;
          DetectionPassShape pass_shape;
          pass_shape.tile_ordinal = tile.ordinal;
          pass_shape.x = tile.x;
          pass_shape.y = tile.y;
          pass_shape.width = tile.width;
          pass_shape.height = tile.height;
          const cv::Mat tile_view = validated.bgr(cv::Rect(
              static_cast<int>(tile.x), static_cast<int>(tile.y),
              static_cast<int>(tile.width), static_cast<int>(tile.height)));
          auto pass_result = run_detection_pass(
              tile_view, tile.width, tile.height, DetectionStrategy::bounded,
              bundle_->tiled_detection->tile_side, pass_limits, true,
              &pass_shape);
          if (!pass_result) {
            return Result<OcrResult>::failure(pass_result.error());
          }
          auto pass_detected = std::move(pass_result).value();
          std::uint32_t updated_contours = 0;
          std::uint32_t updated_raw_boxes = 0;
          if (!internal::checked_add(total_contours,
                                     pass_detected.total_contours,
                                     &updated_contours) ||
              !internal::checked_add(raw_detection_boxes,
                                     pass_shape.raw_candidates,
                                     &updated_raw_boxes) ||
              updated_contours > info_.limits.max_detection_candidates ||
              updated_raw_boxes > info_.limits.max_detection_candidates ||
              pass_detected.boxes.size() != pass_detected.scores.size()) {
            return failure<OcrResult>(
                ErrorCode::resource_limit_exceeded,
                "Tiled detection candidates exceed the effective limit");
          }
          total_contours = updated_contours;
          raw_detection_boxes = updated_raw_boxes;
          for (std::size_t index = 0; index < pass_detected.boxes.size(); ++index) {
            auto candidate_result = internal::make_tiled_candidate(
                pass_detected.boxes[index], pass_detected.scores[index], tile,
                image.width, image.height, static_cast<std::uint32_t>(index),
                bundle_->tiled_detection->artificial_boundary_margin);
            if (!candidate_result) {
              return Result<OcrResult>::failure(candidate_result.error());
            }
            raw_candidates.push_back(std::move(candidate_result).value());
          }
          if (options.include_diagnostics) {
            detection_passes.push_back(pass_shape);
          }
          max_live_detection_pass_buffers = 1;
        }

        stage_begin = Clock::now();
        auto merge_result = internal::merge_tiled_candidates(
            std::move(raw_candidates), *bundle_->tiled_detection);
        stage_end = Clock::now();
        timing.detection_merge_us = elapsed_us(stage_begin, stage_end);
        if (!merge_result) {
          return Result<OcrResult>::failure(merge_result.error());
        }
        auto merged = std::move(merge_result).value();
        suppressed_duplicate_boxes = merged.suppressed_duplicates;
        detected.contour_candidates = total_contours;
        detected.total_contours = total_contours;
        detected.boxes.reserve(merged.representatives.size());
        detected.scores.reserve(merged.representatives.size());
        for (auto& representative : merged.representatives) {
          detected.boxes.push_back(std::move(representative.global_quad));
          detected.scores.push_back(
              static_cast<float>(representative.db_score));
        }
      } else {
        DetectionPassShape pass_shape;
        pass_shape.width = image.width;
        pass_shape.height = image.height;
        auto detected_result = run_detection_pass(
            validated.bgr, image.width, image.height,
            info_.detection_strategy, detection_max_side, detection_limits,
            false, &pass_shape);
        if (!detected_result) {
          return Result<OcrResult>::failure(detected_result.error());
        }
        detected = std::move(detected_result).value();
        raw_detection_boxes = pass_shape.raw_candidates;
        max_live_detection_pass_buffers = 1;
        if (options.include_diagnostics) {
          detection_passes.push_back(pass_shape);
        }
      }

      stage_begin = Clock::now();
      auto sorted_boxes =
          internal::sort_reading_order(std::move(detected.boxes), bundle_->geometry);
      auto plans_result = internal::plan_recognition_batches(
          sorted_boxes, bundle_->geometry, bundle_->recognition, batch_size,
          info_.limits, recognition_width_multiple_,
          recognition_width_buckets_);
      stage_end = Clock::now();
      timing.crop_and_sort_us = elapsed_us(stage_begin, stage_end);
      if (!plans_result) {
        return Result<OcrResult>::failure(plans_result.error());
      }
      auto plans = std::move(plans_result).value();

      std::vector<internal::DecodedText> decoded(sorted_boxes.size());
      std::vector<RecognitionBatchShape> recognition_batch_shapes;
      if (options.include_diagnostics) {
        recognition_batch_shapes.reserve(plans.size());
      }
      for (const auto& plan : plans) {
        std::vector<Quad> batch_boxes;
        batch_boxes.reserve(plan.samples.size());
        for (const auto& sample : plan.samples) {
          batch_boxes.push_back(sorted_boxes[sample.input_index]);
        }

        auto crop_limits = info_.limits;
        crop_limits.max_temporary_bytes -= image_bytes;
        stage_begin = Clock::now();
        auto crops_result = internal::crop_text_regions(
            validated.bgr, batch_boxes, bundle_->geometry, crop_limits);
        stage_end = Clock::now();
        timing.crop_and_sort_us += elapsed_us(stage_begin, stage_end);
        if (!crops_result) {
          return Result<OcrResult>::failure(crops_result.error());
        }
        auto crops = std::move(crops_result).value();
        std::uint64_t crop_bytes = 0;
        for (const auto& crop : crops) {
          std::uint64_t bytes = 0;
          if (!internal::checked_mul<std::uint64_t>(
                  crop.total(), crop.elemSize(), &bytes) ||
              !internal::checked_add<std::uint64_t>(crop_bytes, bytes,
                                                    &crop_bytes) ||
              crop_bytes > crop_limits.max_temporary_bytes) {
            return failure<OcrResult>(
                ErrorCode::resource_limit_exceeded,
                "Recognition crops exceed the request memory budget");
          }
        }

        auto recognition_limits = info_.limits;
        recognition_limits.max_temporary_bytes -= image_bytes;
        recognition_limits.max_temporary_bytes -= crop_bytes;
        stage_begin = Clock::now();
        auto batch_result = internal::make_recognition_batch(
            crops, plan, bundle_->recognition, recognition_limits,
            recognition_width_multiple_, recognition_width_buckets_);
        stage_end = Clock::now();
        timing.recognition_preprocess_us += elapsed_us(stage_begin, stage_end);
        if (!batch_result) {
          return Result<OcrResult>::failure(batch_result.error());
        }
        auto batch = std::move(batch_result).value();
        if (options.include_diagnostics) {
          const auto width = static_cast<std::uint32_t>(batch.shape[3]);
          const bool coreml =
              info_.execution.recognition.runtime == "Core ML";
          const bool ane =
              coreml &&
              info_.execution.recognition.device.find("ane") !=
                  std::string::npos &&
              info_.execution.cpu_partition == CpuPartition::allow &&
              width <= bundle_->apple_provider->recognition_ane_maximum_width;
          recognition_batch_shapes.push_back(
              RecognitionBatchShape{static_cast<std::uint32_t>(batch.shape[0]),
                                    static_cast<std::uint32_t>(batch.shape[2]),
                                    width,
                                    coreml ? (ane ? "ane" : "gpu") : "cpu",
                                    info_.execution.recognition.model_id,
                                    coreml
                                        ? apple_recognition_function_name(width)
                                        : "dynamic"});
        }
        std::vector<cv::Mat>().swap(crops);

        stage_begin = Clock::now();
        auto output_result = recognition_->run(batch.values, batch.shape);
        stage_end = Clock::now();
        timing.recognition_inference_us += elapsed_us(stage_begin, stage_end);
        if (!output_result) {
          return Result<OcrResult>::failure(output_result.error());
        }
        auto output = std::move(output_result).value();
        std::vector<float>().swap(batch.values);

        stage_begin = Clock::now();
        auto batch_decoded_result = internal::decode_ctc(
            output.data(), output.size(), output.shape(),
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
      validated.bgr.release();

      auto assembled = internal::assemble_ocr_result(
          image.width, image.height, bundle_->id, timing,
          detected.contour_candidates, std::move(sorted_boxes), std::move(decoded),
          score_threshold, options.include_diagnostics);
      if (!assembled) return Result<OcrResult>::failure(assembled.error());
      auto result = std::move(assembled).value();
      if (result.diagnostics) {
        result.diagnostics->detection_input_width = detection_input_width;
        result.diagnostics->detection_input_height = detection_input_height;
        result.diagnostics->raw_detection_boxes = raw_detection_boxes;
        result.diagnostics->suppressed_duplicate_boxes =
            suppressed_duplicate_boxes;
        result.diagnostics->max_live_detection_pass_buffers =
            max_live_detection_pass_buffers;
        result.diagnostics->detection_passes = std::move(detection_passes);
        result.diagnostics->recognition_batch_shapes =
            std::move(recognition_batch_shapes);
      }
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
  std::unique_ptr<internal::InferenceSession> detection_;
  std::unique_ptr<internal::InferenceSession> recognition_;
  EngineInfo info_;
  std::uint32_t recognition_width_multiple_ = 1;
  std::vector<std::uint32_t> recognition_width_buckets_;
  std::uint32_t maximum_backend_batch_size_ = 1;
  mutable std::mutex state_mutex_;
  std::condition_variable state_changed_;
  bool active_ = false;
  bool closing_ = false;
};

}  // namespace

Engine::~Engine() noexcept = default;

internal::RuntimePolicy internal::builtin_runtime_policy() {
  RuntimePolicy policy;
#if defined(LIGHT_OCR_HAS_WEBGPU)
  policy.id = "builtin-webgpu-v1";
  policy.ordered_candidates = {"webgpu", "cpu"};
#if defined(LIGHT_OCR_WEBGPU_QUALIFICATION_BUILD)
  policy.qualification_only = true;
  policy.released = false;
#endif
#else
  policy.id = "builtin-cpu-v1";
  policy.ordered_candidates = {"cpu"};
#endif
  policy.version = 1;
  policy.available_providers = {"cpu"};
  policy.provider_qualification_ids = {"builtin-cpu-v1"};
#if defined(LIGHT_OCR_HAS_COREML)
  policy.available_providers.push_back("apple");
  policy.provider_qualification_ids.push_back("builtin-apple-v1");
#endif
#if defined(LIGHT_OCR_HAS_WEBGPU)
  policy.available_providers.push_back("webgpu");
  policy.provider_qualification_ids.push_back("builtin-webgpu-v1");
#endif
  return policy;
}

Result<std::unique_ptr<Engine>> Engine::create(ModelBundle bundle,
                                               const EngineOptions& options) {
  return internal::EngineFactory::create(
      std::move(bundle), options, internal::builtin_runtime_policy());
}

Result<std::unique_ptr<Engine>> internal::EngineFactory::create(
    ModelBundle bundle, const EngineOptions& options,
    RuntimePolicy runtime_policy) {
  try {
    if (!bundle.data_) {
      return failure<std::unique_ptr<Engine>>(ErrorCode::invalid_model_bundle,
                                              "Model bundle has no validated data");
    }
    if (options.intra_op_threads == 0 || options.inter_op_threads == 0) {
      return failure<std::unique_ptr<Engine>>(ErrorCode::invalid_argument,
                                              "ONNX Runtime thread counts must be positive");
    }
    if (!valid_execution_options(options.execution)) {
      return failure<std::unique_ptr<Engine>>(
          ErrorCode::invalid_argument,
          "Execution options are unsupported");
    }
    if (!valid_runtime_policy(runtime_policy)) {
      return failure<std::unique_ptr<Engine>>(
          ErrorCode::internal_error,
          "The package runtime policy is invalid");
    }
    auto selected_provider = options.execution.provider;
    std::vector<std::string> policy_candidates;
    if (selected_provider == ExecutionProvider::automatic) {
      policy_candidates = runtime_policy.ordered_candidates;
    } else {
      const std::string requested = provider_name(selected_provider);
      if (!policy_includes_provider(runtime_policy, requested)) {
        return failure<std::unique_ptr<Engine>>(
            ErrorCode::unsupported_capability,
            "The requested provider is not included in this runtime package",
            requested);
      }
      policy_candidates = {requested};
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
        batch_size > bundle.data_->recognition.maximum_batch_size ||
        (selected_provider == ExecutionProvider::apple && batch_size != 1)) {
      return failure<std::unique_ptr<Engine>>(ErrorCode::invalid_argument,
                                              "Engine recognition defaults are outside limits");
    }
    const auto detection_strategy = options.detection.strategy.value_or(
        bundle.data_->default_detection_strategy);
    if (detection_strategy == DetectionStrategy::tiled &&
        !bundle.data_->tiled_detection) {
      return failure<std::unique_ptr<Engine>>(
          ErrorCode::unsupported_capability,
          "Tiled detection is unavailable in this bundle");
    }
    if (selected_provider == ExecutionProvider::apple &&
        detection_strategy != DetectionStrategy::bounded) {
      return failure<std::unique_ptr<Engine>>(
          ErrorCode::invalid_argument,
          "The Apple provider requires bounded detection");
    }
    const auto default_detection_max_side =
        detection_strategy == bundle.data_->default_detection_strategy
            ? bundle.data_->default_detection_max_side
            : detection_strategy == DetectionStrategy::bounded
                  ? 960u
                  : detection_strategy == DetectionStrategy::tiled
                        ? bundle.data_->tiled_detection->tile_side
                  : bundle.data_->detection.max_side_limit;
    const auto detection_max_side = options.detection.max_side.value_or(
        default_detection_max_side);
    if ((detection_strategy != DetectionStrategy::bounded &&
         detection_strategy != DetectionStrategy::tiled &&
         detection_strategy != DetectionStrategy::upstream_exact) ||
        (detection_strategy == DetectionStrategy::bounded &&
         (detection_max_side < bundle.data_->detection.minimum_dimension ||
          detection_max_side % bundle.data_->detection.dimension_multiple != 0 ||
          detection_max_side > bundle.data_->detection.max_side_limit ||
          detection_max_side > limits.max_detection_side)) ||
        (detection_strategy == DetectionStrategy::upstream_exact &&
         (options.detection.max_side.has_value() ||
          detection_max_side != bundle.data_->detection.max_side_limit ||
          detection_max_side > limits.max_detection_side)) ||
        (detection_strategy == DetectionStrategy::tiled &&
         (options.detection.max_side.has_value() ||
          detection_max_side != bundle.data_->tiled_detection->tile_side ||
          detection_max_side > limits.max_detection_side ||
          limits.max_detection_tiles == 0))) {
      return failure<std::unique_ptr<Engine>>(
          ErrorCode::invalid_argument,
          "Engine detection defaults are outside limits");
    }
    if (selected_provider == ExecutionProvider::apple &&
        detection_max_side > 960) {
      return failure<std::unique_ptr<Engine>>(
          ErrorCode::invalid_argument,
          "The Apple detector is qualified only through side length 960");
    }
    const auto& detection_bytes =
        bundle.data_->files.at(bundle.data_->detection_model_path);
    const auto& recognition_bytes =
        bundle.data_->files.at(bundle.data_->recognition_model_path);
    internal::InferenceSessionConfig detection_config;
    detection_config.intra_op_threads = options.intra_op_threads;
    detection_config.inter_op_threads = options.inter_op_threads;
    detection_config.session_fallback = options.execution.session_fallback;
    detection_config.cpu_partition = options.execution.cpu_partition;
    detection_config.device_id = options.execution.device_id;
    detection_config.performance_hint = options.execution.performance_hint;
    detection_config.precision = options.execution.precision;
    detection_config.model_id = bundle.data_->detection_model_id;
    detection_config.model_sha256 = bundle.data_->detection_model_sha256;
    detection_config.shape_policy = "dynamic";
    detection_config.requested_provider_override =
        provider_name(options.execution.provider);
    auto recognition_config = detection_config;
    recognition_config.model_id = bundle.data_->recognition_model_id;
    recognition_config.model_sha256 = bundle.data_->recognition_model_sha256;

    bool apple_device_available = false;
    bool apple_device_validated = false;
#if defined(LIGHT_OCR_HAS_COREML)
    bool apple_device_allowed = false;
    apple_device_available = internal::coreml_device_available();
    apple_device_validated =
        apple_device_available && bundle.data_->apple_provider &&
        internal::coreml_device_is_validated(
            bundle.data_->apple_provider->validated_device_families);
    apple_device_allowed =
        apple_device_available && bundle.data_->apple_provider &&
        internal::coreml_device_is_allowed(
            bundle.data_->apple_provider->device_policy,
            bundle.data_->apple_provider->architectures,
            bundle.data_->apple_provider->validated_device_families);
#endif

    auto selection = internal::select_candidate<CreatedSessions>(
        provider_name(options.execution.provider), policy_candidates,
        options.execution.provider == ExecutionProvider::automatic
            ? std::optional<std::string>{runtime_policy.id}
            : std::nullopt,
        options.execution.provider == ExecutionProvider::automatic
            ? std::optional<std::uint32_t>{runtime_policy.version}
            : std::nullopt,
        [&](const std::string& candidate) {
          auto fail = [](Error error, CreationReason reason) {
            return internal::CandidateResult<CreatedSessions>::failure(
                internal::CandidateFailure{std::move(error), reason});
          };
          auto fail_from_error = [](
                                     Error error,
                                     std::optional<CreationReason> reason) {
            return internal::CandidateResult<CreatedSessions>::failure(
                internal::CandidateFailure{std::move(error), reason});
          };
          CreatedSessions created;
          if (candidate == "cpu" || candidate == "webgpu") {
            created.provider = candidate == "webgpu"
                                   ? ExecutionProvider::webgpu
                                   : ExecutionProvider::cpu;
            if (created.provider == ExecutionProvider::webgpu &&
                options.execution.cpu_partition == CpuPartition::forbid &&
                bundle.data_->webgpu_provider) {
              return fail(
                  Error{ErrorCode::unsupported_capability,
                        "The WebGPU model requires a bounded CPU operator partition",
                        "required operators: Concat, Gather, Slice"},
                  CreationReason::model_compute_unsupported);
            }
#if !defined(LIGHT_OCR_HAS_WEBGPU)
            if (created.provider == ExecutionProvider::webgpu) {
              return fail(
                  Error{ErrorCode::unsupported_capability,
                        "The runtime descriptor and addon WebGPU capabilities disagree",
                        {}},
                  CreationReason::provider_abi_mismatch);
            }
#endif
            auto candidate_detection_config = detection_config;
            candidate_detection_config.provider = created.provider;
            candidate_detection_config.qualification_id =
                policy_qualification_id(runtime_policy, candidate);
            candidate_detection_config.webgpu_provider_library =
                runtime_policy.webgpu_provider_library;
            candidate_detection_config.webgpu_provider_bytes =
                runtime_policy.webgpu_provider_bytes;
            candidate_detection_config.webgpu_provider_sha256 =
                runtime_policy.webgpu_provider_sha256;
            candidate_detection_config.webgpu_device_validated =
                runtime_policy.released && !runtime_policy.qualification_only;
            auto candidate_recognition_config = recognition_config;
            candidate_recognition_config.provider = created.provider;
            candidate_recognition_config.qualification_id =
                candidate_detection_config.qualification_id;
            candidate_recognition_config.webgpu_provider_library =
                candidate_detection_config.webgpu_provider_library;
            candidate_recognition_config.webgpu_provider_bytes =
                candidate_detection_config.webgpu_provider_bytes;
            candidate_recognition_config.webgpu_provider_sha256 =
                candidate_detection_config.webgpu_provider_sha256;
            candidate_recognition_config.webgpu_device_validated =
                candidate_detection_config.webgpu_device_validated;
            auto candidate_detection_bytes = detection_bytes;
            auto candidate_recognition_bytes = recognition_bytes;
            std::optional<CreationReason> creation_reason;
            auto candidate_detection = internal::OnnxSession::create(
                candidate_detection_bytes, candidate_detection_config,
                internal::ModelKind::detection, 0, &creation_reason);
            if (!candidate_detection) {
              return fail_from_error(candidate_detection.error(),
                                     creation_reason);
            }
            auto candidate_recognition = internal::OnnxSession::create(
                candidate_recognition_bytes, candidate_recognition_config,
                internal::ModelKind::recognition,
                bundle.data_->recognition.characters.size() + 1,
                &creation_reason);
            if (!candidate_recognition) {
              return fail_from_error(candidate_recognition.error(),
                                     creation_reason);
            }
            created.detection = std::move(candidate_detection).value();
            created.recognition = std::move(candidate_recognition).value();
            created.maximum_backend_batch_size =
                bundle.data_->recognition.maximum_batch_size;
            return internal::CandidateResult<CreatedSessions>::success(
                std::move(created));
          }
          if (candidate != "apple") {
            return fail(Error{ErrorCode::internal_error,
                              "Runtime policy contains an unknown provider", {}},
                        CreationReason::internal_assertion_failed);
          }
          created.provider = ExecutionProvider::apple;
          if (!bundle.data_->apple_provider) {
            return fail(
                Error{ErrorCode::unsupported_capability,
                      "The model bundle does not include the Apple provider payload",
                      {}},
                CreationReason::model_compute_unsupported);
          }
#if defined(LIGHT_OCR_HAS_COREML)
          if (!apple_device_available) {
            return fail(
                Error{ErrorCode::unsupported_capability,
                      "The Apple provider has no compatible device", {}},
                CreationReason::adapter_unavailable);
          }
          if (!apple_device_allowed || batch_size != 1 ||
              detection_strategy != DetectionStrategy::bounded ||
              detection_max_side > 960) {
            return fail(
                Error{ErrorCode::unsupported_capability,
                      "The Apple provider cannot create the requested model profile",
                      {}},
                CreationReason::model_compute_unsupported);
          }
          const auto& apple = *bundle.data_->apple_provider;
          auto apple_detection_config = detection_config;
          apple_detection_config.provider = ExecutionProvider::apple;
          apple_detection_config.model_id = apple.detection.model_id;
          apple_detection_config.model_sha256 = apple.detection.package_sha256;
          apple_detection_config.shape_policy = apple.detection.shape_policy;
          apple_detection_config.apple_package = make_apple_package(
              *bundle.data_, apple.detection, apple, false);
          apple_detection_config.apple_package->qualification_id =
              policy_qualification_id(runtime_policy, candidate);
          auto apple_recognition_config = recognition_config;
          apple_recognition_config.provider = ExecutionProvider::apple;
          apple_recognition_config.model_id = apple.recognition.model_id;
          apple_recognition_config.model_sha256 =
              apple.recognition.package_sha256;
          apple_recognition_config.shape_policy = apple.recognition.shape_policy;
          apple_recognition_config.apple_package = make_apple_package(
              *bundle.data_, apple.recognition, apple, true);
          apple_recognition_config.apple_package->qualification_id =
              apple_detection_config.apple_package->qualification_id;
          std::optional<CreationReason> creation_reason;
          auto apple_detection = internal::CoreMlSession::create(
              apple_detection_config, internal::ModelKind::detection,
              &creation_reason);
          if (!apple_detection) {
            return fail_from_error(apple_detection.error(), creation_reason);
          }
          auto apple_recognition = internal::CoreMlSession::create(
              apple_recognition_config, internal::ModelKind::recognition,
              &creation_reason);
          if (!apple_recognition) {
            return fail_from_error(apple_recognition.error(), creation_reason);
          }
          created.detection = std::move(apple_detection).value();
          created.recognition = std::move(apple_recognition).value();
          created.recognition_width_multiple =
              apple.recognition_width_multiple;
          created.recognition_width_buckets =
              apple.recognition_runtime_width_buckets;
          created.maximum_backend_batch_size = 1;
          return internal::CandidateResult<CreatedSessions>::success(
              std::move(created));
#else
          return fail(
              Error{ErrorCode::unsupported_capability,
                    "The runtime descriptor and addon Apple capabilities disagree",
                    {}},
              CreationReason::provider_abi_mismatch);
#endif
        });
    if (!selection.value) {
      return Result<std::unique_ptr<Engine>>::failure(std::move(selection.error));
    }
    auto created = std::move(*selection.value);
    selected_provider = created.provider;
    auto detection = std::move(created.detection);
    auto recognition = std::move(created.recognition);
    const auto recognition_width_multiple =
        created.recognition_width_multiple;
    auto recognition_width_buckets =
        std::move(created.recognition_width_buckets);
    const auto maximum_backend_batch_size =
        created.maximum_backend_batch_size;

    EngineInfo info;
    info.core_version = LIGHT_OCR_VERSION;
    info.model_bundle_id = bundle.data_->id;
    info.model_bundle_schema_version = bundle.data_->schema_version;
    info.normalized_config_schema_version =
        bundle.data_->normalized_config_schema_version;
    info.backend = detection->execution_info().runtime + " " +
                   detection->execution_info().runtime_version;
    info.execution_provider =
        detection->execution_info().runtime == "Core ML"
            ? "CoreML"
            : selected_provider == ExecutionProvider::webgpu
                  ? "WebGpuExecutionProvider"
                  : "CPUExecutionProvider";
    info.execution.requested_provider = options.execution.provider;
    info.execution.session_fallback = options.execution.session_fallback;
    info.execution.cpu_partition = options.execution.cpu_partition;
    info.execution.device_id = options.execution.device_id;
    info.execution.performance_hint = options.execution.performance_hint;
    info.execution.requested_precision = options.execution.precision;
    info.execution.provider_capabilities = {
        ProviderCapabilityInfo{"cpu", true, true, true}};
    if (policy_includes_provider(runtime_policy, "webgpu")) {
      info.execution.provider_capabilities.push_back(
          ProviderCapabilityInfo{"webgpu", true,
                                 selected_provider == ExecutionProvider::webgpu,
                                 selected_provider == ExecutionProvider::webgpu &&
                                     runtime_policy.released &&
                                     !runtime_policy.qualification_only});
    }
    info.execution.selection_trace = std::move(selection.trace);
    if (policy_includes_provider(runtime_policy, "apple") &&
        bundle.data_->apple_provider) {
      info.execution.provider_capabilities.push_back(
          ProviderCapabilityInfo{"apple",
                                 true,
                                 apple_device_available,
                                 apple_device_validated});
    }
    info.execution.detection = detection->execution_info();
    info.execution.recognition = recognition->execution_info();
    info.capabilities = bundle.data_->capabilities;
    info.limits = limits;
    info.intra_op_threads = options.intra_op_threads;
    info.inter_op_threads = options.inter_op_threads;
    info.detection_strategy = detection_strategy;
    info.detection_max_side = detection_max_side;
    if (detection_strategy == DetectionStrategy::tiled) {
      const auto& tiled = *bundle.data_->tiled_detection;
      info.tiled_detection = TiledDetectionInfo{
          tiled.contract_version, tiled.tile_side, tiled.minimum_overlap,
          tiled.artificial_boundary_margin,
          static_cast<float>(tiled.merge_iou_threshold),
          static_cast<float>(tiled.merge_ios_threshold)};
    }
    info.default_recognition_score_threshold = score_threshold;
    info.default_recognition_batch_size = batch_size;
    auto runtime_bundle =
        std::make_shared<internal::BundleData>(*bundle.data_);
    runtime_bundle->files.clear();
    return Result<std::unique_ptr<Engine>>::success(std::unique_ptr<Engine>(new EngineImpl(
        std::move(runtime_bundle), std::move(detection), std::move(recognition),
        std::move(info), recognition_width_multiple,
        std::move(recognition_width_buckets),
        maximum_backend_batch_size)));
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
