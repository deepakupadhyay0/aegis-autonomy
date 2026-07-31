#pragma once

#include "common/logging/log_protocol.hpp"

#include <cstdint>

namespace logging
{

inline constexpr uint32_t DEFAULT_LOG_QUEUE_CAPACITY = 4096U;

using log_level_e = common::logging::log_level_e;

struct logging_options_s
{
  log_level_e minimum_level{log_level_e::info};
  uint32_t queue_capacity{DEFAULT_LOG_QUEUE_CAPACITY};
  bool enable_console_log{false};
};

struct logging_service_options_s
{
  /// Disabled by default so the service creates no ROS context or DDS entity.
  bool enable_diagnostic_dds{false};
};

}  // namespace logging
