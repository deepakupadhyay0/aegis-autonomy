#pragma once

#include "ai_diagnostics/ai_diagnostics_config.hpp"
#include "ai_diagnostics/diagnostic_types.hpp"
#include "ai_diagnostics/visibility_control.hpp"
#include "common/cancellable_curl.hpp"

#include <optional>

namespace ai_diagnostics
{

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
  void cancel() noexcept override;

private:
  common::string256_t m_endpoint;
  common::string64_t m_model;
  common::string64_t m_api_key_environment;
  std::chrono::milliseconds m_request_timeout;
  std::size_t m_maximum_response_bytes;
  common::fixed_string_c<4096U> m_diagnostic_memory;
  common::cancellable_curl_c m_curl;
};

}  // namespace ai_diagnostics
