#pragma once

#include "common/fixed_string.hpp"
#include "common/numeric_types.hpp"
#include "utils/visibility_control.hpp"

#include <chrono>
#include <cstddef>
#include <memory>

namespace utils
{

enum class topic_timing_severity_e : common::uint8_t
{
  initializing = 0U,
  healthy,
  warning,
  error,
  fatal
};

enum class topic_timing_issue_e : common::uint16_t
{
  none = 0U,
  startup_timeout = 1U << 0U,
  message_timeout = 1U << 1U,
  rate_low = 1U << 2U,
  rate_high = 1U << 3U,
  delay_mean_high = 1U << 4U,
  delay_jitter_high = 1U << 5U,
  acquisition_time_regression = 1U << 6U,
  receive_time_regression = 1U << 7U,
  duplicate_acquisition_time = 1U << 8U,
  future_acquisition_time = 1U << 9U,
  negative_transport_delay = 1U << 10U,
  watchdog_time_regression = 1U << 11U,
  dds_deadline_missed = 1U << 12U,
  dds_liveliness_lost = 1U << 13U,
  dds_message_lost = 1U << 14U,
  dds_incompatible_qos = 1U << 15U
};

constexpr topic_timing_issue_e operator|(
  const topic_timing_issue_e left,
  const topic_timing_issue_e right) noexcept
{
  return static_cast<topic_timing_issue_e>(
    static_cast<common::uint32_t>(left) |
    static_cast<common::uint32_t>(right));
}

struct topic_timing_options_s
{
  common::string128_t topic_name;
  common::uint32_t window_sample_count{100U};
  common::uint32_t startup_sample_count{10U};
  common::float64_t expected_rate_hz{30.0};
  std::chrono::milliseconds startup_timeout{2000};
  std::chrono::milliseconds warning_timeout{100};
  std::chrono::milliseconds error_timeout{500};
  common::float64_t rate_low_warning_percent{90.0};
  common::float64_t rate_low_error_percent{70.0};
  common::float64_t rate_high_warning_percent{110.0};
  common::float64_t rate_high_error_percent{130.0};
  common::float64_t delay_mean_warning_percent{20.0};
  common::float64_t delay_mean_error_percent{50.0};
  common::float64_t delay_jitter_warning_percent{10.0};
  common::float64_t delay_jitter_error_percent{30.0};
};

struct topic_timing_result_s
{
  common::string128_t topic_name;
  topic_timing_severity_e severity{topic_timing_severity_e::initializing};
  topic_timing_issue_e issues{topic_timing_issue_e::none};
  common::uint32_t sample_count{0U};
  common::float64_t measured_rate_hz{0.0};
  common::float64_t mean_delay_ms{0.0};
  common::float64_t delay_jitter_ms{0.0};
  common::int64_t latest_message_age_ms{0};
};

struct topic_timing_alert_s
{
  topic_timing_result_s result;
  common::uint64_t sequence{0U};
};

/// Capacity-one alert queue that preserves the highest pending severity.
class UTILS_PUBLIC topic_timing_alert_queue_c final
{
public:
  explicit topic_timing_alert_queue_c(std::size_t capacity = 1U);
  ~topic_timing_alert_queue_c() noexcept;

  topic_timing_alert_queue_c(const topic_timing_alert_queue_c &) = delete;
  topic_timing_alert_queue_c & operator=(const topic_timing_alert_queue_c &) = delete;
  topic_timing_alert_queue_c(topic_timing_alert_queue_c &&) = delete;
  topic_timing_alert_queue_c & operator=(topic_timing_alert_queue_c &&) = delete;

  bool try_push(const topic_timing_alert_s & alert);
  bool try_pop(topic_timing_alert_s & alert);
  void clear();

private:
  class implementation_c;
  std::unique_ptr<implementation_c> m_implementation;
};

/// Bounded, single-owner monitor for topic timing and DDS status faults.
class UTILS_PUBLIC topic_timing_monitor_c final
{
public:
  using timestamp_t = std::chrono::nanoseconds;

  topic_timing_monitor_c(
    const topic_timing_options_s & options,
    timestamp_t start_watchdog_time);
  ~topic_timing_monitor_c() noexcept;

  topic_timing_monitor_c(const topic_timing_monitor_c &) = delete;
  topic_timing_monitor_c & operator=(const topic_timing_monitor_c &) = delete;
  topic_timing_monitor_c(topic_timing_monitor_c &&) = delete;
  topic_timing_monitor_c & operator=(topic_timing_monitor_c &&) = delete;

  void record_message(
    timestamp_t receive_watchdog_time,
    timestamp_t receive_timestamp,
    timestamp_t acquisition_timestamp) noexcept;
  void record_dds_issue(topic_timing_issue_e issue) noexcept;
  topic_timing_result_s evaluate(timestamp_t current_watchdog_time) noexcept;

private:
  class implementation_c;
  std::unique_ptr<implementation_c> m_implementation;
};

}  // namespace utils
