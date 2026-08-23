#pragma once

#include "ai_diagnostics/visibility_control.hpp"
#include "common/fixed_string.hpp"
#include "common/numeric_types.hpp"
#include "logging/log_types.hpp"

#include <chrono>
#include <cstddef>

namespace ai_diagnostics
{

enum class diagnostics_input_mode_e : common::uint8_t
{
  dds,
  log_files
};

struct ai_diagnostics_options_s
{
  bool enabled{false};
  diagnostics_input_mode_e input_mode{diagnostics_input_mode_e::dds};
  common::string256_t input_topic;
  common::string256_t report_topic;
  common::string256_t log_directory;
  common::string64_t cursor_filename;
  logging::log_level_e minimum_log_level{logging::log_level_e::info};
  common::uint32_t max_log_files{0U};
  common::uint32_t max_records_per_batch{0U};
  common::uint32_t queue_capacity{0U};
  std::size_t maximum_context_bytes{0U};
  common::float64_t cpu_threshold_percent{0.0};
  common::uint32_t idle_samples_required{0U};
  std::chrono::milliseconds sample_interval{0};
  std::chrono::milliseconds analysis_cooldown{0};
  std::chrono::milliseconds request_timeout{0};
  std::size_t maximum_response_bytes{0U};
  common::string256_t endpoint;
  common::string64_t model;
  common::string64_t api_key_environment;
};

AI_DIAGNOSTICS_PUBLIC ai_diagnostics_options_s load_ai_diagnostics_options();

}  // namespace ai_diagnostics
