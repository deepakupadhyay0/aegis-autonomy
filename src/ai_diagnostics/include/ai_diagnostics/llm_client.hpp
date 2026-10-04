#pragma once

#include "ai_diagnostics/ai_diagnostics_config.hpp"
#include "ai_diagnostics/diagnostic_types.hpp"
#include "ai_diagnostics/visibility_control.hpp"
#include "common/cancellable_curl.hpp"
#include "common/numeric_types.hpp"

#include <optional>
#include <string>

namespace ai_diagnostics
{

struct llm_attempt_diagnostics_s
{
  std::string failure_reason;
  std::string response_excerpt;
  common::int64_t http_status{0};
};

class AI_DIAGNOSTICS_PUBLIC llm_client_c
{
public:
  llm_client_c() = default;
  virtual ~llm_client_c() noexcept = default;

  llm_client_c(const llm_client_c &) = delete;
  llm_client_c & operator=(const llm_client_c &) = delete;
  llm_client_c(llm_client_c &&) = delete;
  llm_client_c & operator=(llm_client_c &&) = delete;

  virtual std::optional<diagnostic_report_s> analyze(
    const diagnostic_batch_s & batch) noexcept = 0;
  virtual void cancel() noexcept = 0;
};

class AI_DIAGNOSTICS_PUBLIC openai_compatible_llm_client_c final
  : public llm_client_c
{
public:
  explicit openai_compatible_llm_client_c(
    const ai_diagnostics_options_s & options);
  ~openai_compatible_llm_client_c() noexcept override = default;

  openai_compatible_llm_client_c(
    const openai_compatible_llm_client_c &) = delete;
  openai_compatible_llm_client_c & operator=(
    const openai_compatible_llm_client_c &) = delete;
  openai_compatible_llm_client_c(
    openai_compatible_llm_client_c &&) = delete;
  openai_compatible_llm_client_c & operator=(
    openai_compatible_llm_client_c &&) = delete;

  std::optional<diagnostic_report_s> analyze(
    const diagnostic_batch_s & batch) noexcept override;
  // The excerpt is bounded but can contain supplied evidence. Inspect it only
  // in a controlled diagnostic session, and do not log it on the node path.
  std::optional<diagnostic_report_s> analyze_with_diagnostics(
    const diagnostic_batch_s & batch,
    llm_attempt_diagnostics_s & diagnostics) noexcept;
  void cancel() noexcept override;

private:
  // diagnostics is borrowed only for this synchronous call.
  std::optional<diagnostic_report_s> analyze_impl(
    const diagnostic_batch_s & batch,
    llm_attempt_diagnostics_s * diagnostics) noexcept;

  common::string256_t m_endpoint;
  common::string64_t m_model;
  common::string64_t m_api_key_environment;
  std::chrono::milliseconds m_request_timeout;
  std::size_t m_maximum_response_bytes;
  std::size_t m_maximum_request_bytes;
  common::cancellable_curl_c m_curl;
};

}  // namespace ai_diagnostics
