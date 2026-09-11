#include "utils/topic_timing_config.hpp"

#include "autonomy_config/topic_timing.hpp"

#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

namespace utils
{

topic_timing_configuration_s load_topic_timing_configuration()
{
  const autonomy_config::TopicTiming config =
    autonomy_config::TopicTiming::get_run_time_values();
  const std::vector<std::string> & topic_names = config.get_topic_names();
  const std::vector<common::float64_t> & expected_rates =
    config.get_expected_rates_hz();
  if (topic_names.size() != expected_rates.size()) {
    throw std::invalid_argument(
            "Topic timing names and expected rates must have equal lengths");
  }

  topic_timing_configuration_s result;
  result.enabled = config.get_enabled();
  result.topics.reserve(topic_names.size());
  for (std::size_t index = 0U; index < topic_names.size(); ++index) {
    topic_timing_options_s options;
    options.topic_name.assign(topic_names[index]);
    options.expected_rate_hz = expected_rates[index];
    options.window_sample_count =
      static_cast<common::uint32_t>(config.get_window_sample_count());
    options.startup_sample_count =
      static_cast<common::uint32_t>(config.get_startup_sample_count());
    options.startup_timeout =
      std::chrono::milliseconds(config.get_startup_timeout_ms());
    options.warning_timeout =
      std::chrono::milliseconds(config.get_warning_timeout_ms());
    options.error_timeout = std::chrono::milliseconds(config.get_error_timeout_ms());
    options.rate_low_warning_percent = config.get_rate_low_warning_percent();
    options.rate_low_error_percent = config.get_rate_low_error_percent();
    options.rate_high_warning_percent = config.get_rate_high_warning_percent();
    options.rate_high_error_percent = config.get_rate_high_error_percent();
    options.delay_mean_warning_percent = config.get_delay_mean_warning_percent();
    options.delay_mean_error_percent = config.get_delay_mean_error_percent();
    options.delay_jitter_warning_percent = config.get_delay_jitter_warning_percent();
    options.delay_jitter_error_percent = config.get_delay_jitter_error_percent();
    result.topics.push_back(options);
  }
  return result;
}

}  // namespace utils
