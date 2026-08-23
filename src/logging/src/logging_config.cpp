#include "logging/logging_config.hpp"

#include "autonomy_config/config_directory.hpp"

#include <autonomy_config/logging.hpp>

#include <cstdlib>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace logging
{

namespace
{

log_level_e parse_log_level(const std::string_view level)
{
  if (level == "debug") {
    return log_level_e::debug;
  }
  if (level == "info") {
    return log_level_e::info;
  }
  if (level == "warning" || level == "warn") {
    return log_level_e::warning;
  }
  if (level == "error") {
    return log_level_e::error;
  }
  if (level == "fatal") {
    return log_level_e::fatal;
  }
  throw std::invalid_argument("Configured logging minimum_level is invalid");
}

common::uint32_t validate_uint32(
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

common::uint64_t validate_file_size(const common::int64_t value)
{
  if (value <= 0 ||
    static_cast<common::uint64_t>(value) >
    std::numeric_limits<std::size_t>::max())
  {
    throw std::invalid_argument(
      "Configured logging file_size_bytes is invalid");
  }
  return static_cast<common::uint64_t>(value);
}

std::chrono::milliseconds validate_diagnostic_interval(
  const common::int64_t value,
  const bool diagnostics_enabled)
{
  if (diagnostics_enabled && value <= 0) {
    throw std::invalid_argument(
      "Configured logging diagnostics_interval_ms is invalid");
  }
  return std::chrono::milliseconds(value > 0 ? value : 0);
}

std::filesystem::path resolve_log_directory(
  const std::string_view configured_directory)
{
  const char * const environment_directory =
    std::getenv("AEGIS_AUTONOMY_LOG_DIR");  // NOLINT(concurrency-mt-unsafe)
  const bool use_environment_directory =
    environment_directory != nullptr && environment_directory[0] != '\0';
  const std::filesystem::path requested_directory =
    use_environment_directory ?
    std::filesystem::path(environment_directory) :
    std::filesystem::path(configured_directory);
  if (requested_directory.empty()) {
    throw std::invalid_argument("Configured logging log_directory is empty");
  }

  std::error_code filesystem_error;
  std::filesystem::path resolved_directory = requested_directory;
  if (requested_directory.is_relative()) {
    const std::filesystem::path base_directory =
      use_environment_directory ?
      std::filesystem::current_path(filesystem_error) :
      autonomy_config::get_config_directory();
    if (filesystem_error || base_directory.empty()) {
      throw std::runtime_error("Failed to resolve logging base directory");
    }
    resolved_directory = base_directory / requested_directory;
  }

  resolved_directory = std::filesystem::absolute(
    resolved_directory,
    filesystem_error);
  if (filesystem_error) {
    throw std::runtime_error("Failed to resolve logging directory");
  }
  return resolved_directory.lexically_normal();
}

common::string256_t make_fixed_log_directory(
  const std::filesystem::path & directory)
{
  const std::string directory_string = directory.string();
  if (directory_string.empty() ||
    directory_string.size() > common::string256_t::capacity())
  {
    throw std::invalid_argument(
      "Resolved logging directory exceeds supported length");
  }
  common::string256_t fixed_directory;
  fixed_directory.assign(directory_string);
  return fixed_directory;
}

}  // namespace

logging_options_s load_logging_options()
{
  const autonomy_config::Logging configured_logging =
    autonomy_config::Logging::get_run_time_values();

  logging_options_s options;
  options.log_directory = make_fixed_log_directory(
    resolve_log_directory(configured_logging.get_log_directory()));
  options.minimum_level = parse_log_level(
    configured_logging.get_minimum_level());
  options.queue_capacity = validate_uint32(
    configured_logging.get_queue_capacity(),
    "Configured logging queue_capacity is invalid");
  options.file_size_bytes = validate_file_size(
    configured_logging.get_file_size_bytes());
  options.max_files = validate_uint32(
    configured_logging.get_max_files(),
    "Configured logging max_files is invalid");
  options.enable_queue_diagnostics =
    configured_logging.get_diagnostics_enabled();
  options.queue_diagnostic_interval = validate_diagnostic_interval(
    configured_logging.get_diagnostics_interval_ms(),
    options.enable_queue_diagnostics);
  options.enable_console_log = configured_logging.get_console_enabled();
  options.enable_diagnostic_dds = configured_logging.get_dds_enabled();
  return options;
}

}  // namespace logging
