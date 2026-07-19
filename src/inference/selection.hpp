#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "light_ocr/error.hpp"

namespace light_ocr::internal {

struct CandidateFailure {
  Error error;
  std::optional<CreationReason> creation_reason;
};

template <class T>
class CandidateResult {
 public:
  static CandidateResult success(T value) {
    return CandidateResult(std::move(value));
  }
  static CandidateResult failure(CandidateFailure failure) {
    return CandidateResult(std::move(failure));
  }

  bool ok() const noexcept { return value_.has_value(); }
  T&& value() && { return std::move(*value_); }
  const CandidateFailure& failure() const { return *failure_; }

 private:
  explicit CandidateResult(T value) : value_(std::move(value)) {}
  explicit CandidateResult(CandidateFailure failure)
      : failure_(std::move(failure)) {}

  std::optional<T> value_;
  std::optional<CandidateFailure> failure_;
};

template <class T>
struct SelectionResult {
  std::optional<T> value;
  Error error;
  CreationTrace trace;
};

inline bool is_skippable_creation_reason(CreationReason reason) noexcept {
  return reason == CreationReason::adapter_unavailable ||
         reason == CreationReason::model_compute_unsupported ||
         reason == CreationReason::device_memory_insufficient ||
         reason == CreationReason::driver_version_unsupported;
}

template <class T, class Factory>
SelectionResult<T> select_candidate(std::string requested_provider,
                                    std::vector<std::string> candidates,
                                    std::optional<std::string> policy_id,
                                    std::optional<std::uint32_t> policy_version,
                                    Factory&& factory) {
  SelectionResult<T> result;
  result.trace.requested_provider = std::move(requested_provider);
  result.trace.policy_id = std::move(policy_id);
  result.trace.policy_version = policy_version;
  result.trace.ordered_candidates = candidates;
  const bool automatic = result.trace.policy_id.has_value();

  for (std::size_t index = 0; index < candidates.size(); ++index) {
    auto candidate = factory(candidates[index]);
    if (candidate.ok()) {
      result.trace.attempts.push_back(CreationAttempt{
          candidates[index], CreationAttemptStatus::selected, std::nullopt,
          std::nullopt});
      result.trace.selected_provider = candidates[index];
      result.value.emplace(std::move(candidate).value());
      return result;
    }

    const auto& failure = candidate.failure();
    const bool continue_allowed =
        automatic && index + 1 < candidates.size() &&
        failure.creation_reason.has_value() &&
        is_skippable_creation_reason(*failure.creation_reason);
    result.trace.attempts.push_back(CreationAttempt{
        candidates[index],
        continue_allowed ? CreationAttemptStatus::skipped
                         : CreationAttemptStatus::fatal,
        failure.creation_reason,
        failure.creation_reason.has_value()
            ? std::optional<ErrorCode>{}
            : std::optional<ErrorCode>{failure.error.code}});
    if (!continue_allowed) {
      result.error = failure.error;
      result.error.creation_trace = result.trace;
      return result;
    }
  }

  result.error = Error{ErrorCode::internal_error,
                       "Runtime policy has no selectable candidate", {},
                       result.trace};
  return result;
}

}  // namespace light_ocr::internal
