#include "test.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/engine_factory.hpp"
#include "inference/selection.hpp"

namespace light_ocr::test {
namespace {

using internal::CandidateFailure;
using internal::CandidateResult;

CandidateFailure typed_failure(CreationReason reason) {
  return CandidateFailure{
      Error{ErrorCode::runtime_initialization_failed, "candidate failed", {}},
      reason};
}

}  // namespace

LIGHT_OCR_TEST(auto_skips_typed_unavailable_candidate_then_selects_cpu) {
  auto result = internal::select_candidate<std::string>(
      "auto", {"webgpu", "cpu"}, "linux-x64-gnu-v1", 1,
      [](const std::string& provider) {
        if (provider == "webgpu") {
          return CandidateResult<std::string>::failure(
              typed_failure(CreationReason::adapter_unavailable));
        }
        return CandidateResult<std::string>::success(provider);
      });
  EXPECT_TRUE(result.value.has_value());
  EXPECT_EQ(*result.value, std::string("cpu"));
  EXPECT_EQ(result.trace.attempts.size(), std::size_t{2});
  EXPECT_EQ(result.trace.attempts[0].status, CreationAttemptStatus::skipped);
  EXPECT_EQ(result.trace.attempts[1].status, CreationAttemptStatus::selected);
}

LIGHT_OCR_TEST(auto_skips_every_recoverable_typed_reason) {
  const std::vector<CreationReason> reasons = {
      CreationReason::adapter_unavailable,
      CreationReason::model_compute_unsupported,
      CreationReason::device_memory_insufficient,
      CreationReason::driver_version_unsupported};
  for (const auto reason : reasons) {
    auto result = internal::select_candidate<std::string>(
        "auto", {"webgpu", "cpu"}, "linux-x64-gnu-v1", 1,
        [reason](const std::string& provider) {
          if (provider == "webgpu") {
            return CandidateResult<std::string>::failure(
                typed_failure(reason));
          }
          return CandidateResult<std::string>::success(provider);
        });
    EXPECT_TRUE(result.value.has_value());
    EXPECT_EQ(*result.value, std::string("cpu"));
    EXPECT_EQ(result.trace.attempts[0].creation_reason,
              std::optional<CreationReason>{reason});
    EXPECT_EQ(result.trace.attempts[0].status,
              CreationAttemptStatus::skipped);
  }
}

LIGHT_OCR_TEST(auto_stops_on_untyped_failure_before_next_candidate) {
  std::size_t calls = 0;
  auto result = internal::select_candidate<std::string>(
      "auto", {"webgpu", "cpu"}, "linux-x64-gnu-v1", 1,
      [&calls](const std::string&) {
        ++calls;
        return CandidateResult<std::string>::failure(CandidateFailure{
            Error{ErrorCode::runtime_initialization_failed,
                  "unclassified candidate failure", {}},
            std::nullopt});
      });
  EXPECT_FALSE(result.value.has_value());
  EXPECT_EQ(calls, std::size_t{1});
  EXPECT_EQ(result.trace.attempts[0].status, CreationAttemptStatus::fatal);
  EXPECT_EQ(*result.trace.attempts[0].error_code,
            ErrorCode::runtime_initialization_failed);
}

LIGHT_OCR_TEST(explicit_provider_never_falls_back) {
  auto result = internal::select_candidate<std::string>(
      "webgpu", {"webgpu"}, std::nullopt, std::nullopt,
      [](const std::string&) {
        return CandidateResult<std::string>::failure(
            typed_failure(CreationReason::adapter_unavailable));
      });
  EXPECT_FALSE(result.value.has_value());
  EXPECT_EQ(result.trace.attempts.size(), std::size_t{1});
  EXPECT_EQ(result.trace.attempts[0].status, CreationAttemptStatus::fatal);
  EXPECT_TRUE(result.error.creation_trace.has_value());
}

LIGHT_OCR_TEST(auto_stops_on_fatal_reason) {
  std::size_t calls = 0;
  auto result = internal::select_candidate<std::string>(
      "auto", {"webgpu", "cpu"}, "linux-x64-gnu-v1", 1,
      [&calls](const std::string&) {
        ++calls;
        return CandidateResult<std::string>::failure(
            typed_failure(CreationReason::artifact_hash_mismatch));
      });
  EXPECT_FALSE(result.value.has_value());
  EXPECT_EQ(calls, std::size_t{1});
  EXPECT_EQ(result.trace.attempts[0].status, CreationAttemptStatus::fatal);
}

LIGHT_OCR_TEST(auto_final_candidate_failure_is_fatal) {
  auto result = internal::select_candidate<std::string>(
      "auto", {"cpu"}, "builtin-cpu-v1", 1,
      [](const std::string&) {
        return CandidateResult<std::string>::failure(CandidateFailure{
            Error{ErrorCode::runtime_initialization_failed, "CPU failed", {}},
            std::nullopt});
      });
  EXPECT_FALSE(result.value.has_value());
  EXPECT_EQ(result.trace.attempts[0].status, CreationAttemptStatus::fatal);
  EXPECT_FALSE(result.trace.attempts[0].creation_reason.has_value());
  EXPECT_EQ(*result.trace.attempts[0].error_code,
            ErrorCode::runtime_initialization_failed);
}

LIGHT_OCR_TEST(builtin_webgpu_policy_places_webgpu_before_cpu_when_bundled) {
  const auto policy = internal::builtin_runtime_policy();
  const auto webgpu = std::find(policy.available_providers.begin(),
                                policy.available_providers.end(), "webgpu");
  if (webgpu == policy.available_providers.end()) {
    EXPECT_EQ(policy.id, std::string("builtin-cpu-v1"));
    EXPECT_EQ(policy.ordered_candidates, std::vector<std::string>{"cpu"});
    return;
  }
  EXPECT_EQ(policy.id, std::string("builtin-webgpu-v1"));
  EXPECT_EQ(policy.ordered_candidates,
            (std::vector<std::string>{"webgpu", "cpu"}));
#if defined(LIGHT_OCR_WEBGPU_QUALIFICATION_BUILD)
  EXPECT_TRUE(policy.qualification_only);
  EXPECT_TRUE(!policy.released);
#else
  EXPECT_TRUE(!policy.qualification_only);
  EXPECT_TRUE(policy.released);
#endif
}

}  // namespace light_ocr::test
