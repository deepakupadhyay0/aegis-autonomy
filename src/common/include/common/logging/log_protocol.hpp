#pragma once

#include "common/fixed_string.hpp"
#include "common/numeric_types.hpp"

namespace common
{
namespace logging
{

enum class log_packet_type_e : uint8_t
{
  registration = 1U,
  record = 2U
};

enum class log_level_e : uint8_t
{
  debug = 0U,
  info,
  warning,
  error,
  fatal
};

struct log_registration_s
{
  string64_t node_name;
  bool enable_console_log{false};
};

struct log_record_s
{
  uint64_t timestamp_ns{0U};
  uint64_t dropped_record_count{0U};
  uint32_t source_line{0U};
  log_level_e level{log_level_e::info};
  string16_t thread_name;
  string64_t source_file;
  string256_t message;
};

}  // namespace logging
}  // namespace common
