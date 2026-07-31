#pragma once

#include "common/fixed_string.hpp"
#include "common/numeric_types.hpp"

#include <chrono>

namespace logging
{

inline constexpr common::uint32_t DEFAULT_LOG_QUEUE_CAPACITY = 4096U;

enum class log_level_e : common::uint8_t
{
  debug = 0U,
  info,
  warning,
  error,
  fatal
};

struct log_record_s
{
  common::uint64_t timestamp_ns{0U};
  common::uint32_t source_line{0U};
  log_level_e level{log_level_e::info};
  common::string16_t thread_name;
  common::string64_t source_file;
  common::string256_t message;
};

struct logging_options_s
{
  common::string256_t log_directory;
  log_level_e minimum_level{log_level_e::info};
  common::uint32_t queue_capacity{DEFAULT_LOG_QUEUE_CAPACITY};
  common::uint64_t file_size_bytes{0U};
  common::uint32_t max_files{0U};
  bool enable_console_log{false};
  bool enable_diagnostic_dds{false};
  bool enable_queue_diagnostics{true};
  std::chrono::milliseconds queue_diagnostic_interval{5000};
};

}  // namespace logging
