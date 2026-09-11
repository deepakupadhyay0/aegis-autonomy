#pragma once

#include "utils/topic_timing_monitor.hpp"
#include "utils/visibility_control.hpp"

#include <vector>

namespace utils
{

struct topic_timing_configuration_s
{
  bool enabled{false};
  std::vector<topic_timing_options_s> topics;
};

/// Loads and validates runtime topic-timing configuration.
UTILS_PUBLIC topic_timing_configuration_s load_topic_timing_configuration();

}  // namespace utils
