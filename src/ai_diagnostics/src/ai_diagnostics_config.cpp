#include "ai_diagnostics/ai_diagnostics_config.hpp"

#include "logging/logging_config.hpp"

#include <autonomy_config/ai_diagnostics.hpp>

#include <limits>
#include <stdexcept>
#include <string_view>

namespace ai_diagnostics
{
namespace
{

common::uint32_t positive_uint32(
  const common::int64_t value,
  const char * const error_message)
{
  if (value <= 0 ||
    static_cast<common::uint64_t>(value) >
    std::numeric_limits<common::uint32_t>::max())
  {
    throw std::invalid_argument(error_message);
  }
  return static_cast<common::uint32_t>(value);
}

logging::log_level_e parse_log_level(const std::string_view level)
{
  if (level == "debug") {
    return logging::log_level_e::debug;
  }
  if (level == "info") {
    return logging::log_level_e::info;
  }
  if (level == "warning" || level == "warn") {
    return logging::log_level_e::warning;
  }
  if (level == "error") {
    return logging::log_level_e::error;
  }
  if (level == "fatal") {
    return logging::log_level_e::fatal;
  }
  throw std::invalid_argument("AI diagnostics minimum log level is invalid");
}

diagnostics_input_mode_e parse_input_mode(const std::string_view mode)
{
  if (mode == "dds") {
    return diagnostics_input_mode_e::dds;
  }
  if (mode == "log_files") {
    return diagnostics_input_mode_e::log_files;
  }
  throw std::invalid_argument("AI diagnostics input mode is invalid");
}

std::chrono::milliseconds positive_duration(
  const common::int64_t value,
  const char * const error_message)
{
  if (value <= 0) {
    throw std::invalid_argument(error_message);
  }
  return std::chrono::milliseconds(value);
}

template<std::size_t capacity_v>
common::fixed_string_c<capacity_v> make_fixed_string(
  const std::string_view value)
{
  common::fixed_string_c<capacity_v> result;
  result.assign(value);
  return result;
}

}  // namespace

ai_diagnostics_options_s load_ai_diagnostics_options()
{
  const autonomy_config::AiDiagnostics config =
    autonomy_config::AiDiagnostics::get_run_time_values();

  ai_diagnostics_options_s options;
  const logging::logging_options_s logging_options =
    logging::load_logging_options();
  options.enabled = config.get_enabled();
  options.input_mode = parse_input_mode(config.get_input_mode());
  options.input_topic = make_fixed_string<256U>(config.get_input_topic());
  options.report_topic = make_fixed_string<256U>(config.get_report_topic());
  options.log_directory = logging_options.log_directory;
  options.cursor_filename = make_fixed_string<64U>(
    config.get_cursor_filename());
  options.minimum_log_level = parse_log_level(
    config.get_minimum_log_level());
  options.max_log_files = logging_options.max_files;
  options.max_records_per_batch = positive_uint32(
    config.get_max_records_per_batch(),
    "AI diagnostics maximum batch record count is invalid");
  options.queue_capacity = positive_uint32(
    config.get_queue_capacity(),
    "AI diagnostics queue capacity is invalid");

  const common::int64_t maximum_context_bytes =
    config.get_maximum_context_bytes();
  if (maximum_context_bytes <= 0 || maximum_context_bytes > 1048576) {
    throw std::invalid_argument(
            "AI diagnostics maximum context size is invalid");
  }
  options.maximum_context_bytes = static_cast<std::size_t>(
    maximum_context_bytes);

  options.cpu_threshold_percent = config.get_cpu_threshold_percent();
  if (options.cpu_threshold_percent < 0.0 ||
    options.cpu_threshold_percent > 100.0)
  {
    throw std::invalid_argument(
            "AI diagnostics CPU threshold is outside [0, 100]");
  }
  options.idle_samples_required = positive_uint32(
    config.get_idle_samples_required(),
    "AI diagnostics idle sample count is invalid");
  options.sample_interval = positive_duration(
    config.get_sample_interval_ms(),
    "AI diagnostics sample interval is invalid");
  options.analysis_cooldown = positive_duration(
    config.get_analysis_cooldown_ms(),
    "AI diagnostics analysis cooldown is invalid");
  options.request_timeout = positive_duration(
    config.get_request_timeout_ms(),
    "AI diagnostics request timeout is invalid");

  const common::int64_t maximum_response_bytes =
    config.get_maximum_response_bytes();
  if (maximum_response_bytes <= 0 ||
    static_cast<common::uint64_t>(maximum_response_bytes) >
    std::numeric_limits<std::size_t>::max() ||
    maximum_response_bytes > 1048576)
  {
    throw std::invalid_argument(
            "AI diagnostics maximum response size is invalid");
  }
  options.maximum_response_bytes = static_cast<std::size_t>(
    maximum_response_bytes);
  options.endpoint = make_fixed_string<256U>(config.get_endpoint());
  options.model = make_fixed_string<64U>(config.get_model());
  options.api_key_environment = make_fixed_string<64U>(
    config.get_api_key_env());

  if (options.report_topic.empty() ||
    (options.input_mode == diagnostics_input_mode_e::dds &&
    options.input_topic.empty()))
  {
    throw std::invalid_argument("AI diagnostics topic name is empty");
  }
  if (options.log_directory.empty() || options.cursor_filename.empty()) {
    throw std::invalid_argument("AI diagnostics log path is empty");
  }
  if (options.enabled &&
    (options.endpoint.empty() || options.model.empty()))
  {
    throw std::invalid_argument(
            "Enabled AI diagnostics requires an endpoint and model");
  }
  return options;
}

}  // namespace ai_diagnostics
