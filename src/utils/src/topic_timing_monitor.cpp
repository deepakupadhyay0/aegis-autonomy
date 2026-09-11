#include "utils/topic_timing_monitor.hpp"

#include "base_core/mutex.hpp"
#include "base_core/ring_buffer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <mutex>
#include <optional>
#include <vector>

namespace utils
{
namespace
{

using duration_t = std::chrono::duration<common::float64_t>;

common::uint32_t issue_bits(const topic_timing_issue_e issue) noexcept
{
  return static_cast<common::uint32_t>(issue);
}

void add_issue(topic_timing_issue_e & issues, const topic_timing_issue_e issue) noexcept
{
  issues = issues | issue;
}

bool has_issue(const topic_timing_issue_e issues, const topic_timing_issue_e issue) noexcept
{
  return (issue_bits(issues) & issue_bits(issue)) != 0U;
}

}  // namespace

class topic_timing_monitor_c::implementation_c final
{
public:
  implementation_c(
    const topic_timing_options_s & options,
    const timestamp_t start_watchdog_time)
  : m_options(validate(options)),
    m_start_watchdog_time(start_watchdog_time),
    m_delays(m_options.window_sample_count, 0.0),
    m_intervals(m_options.window_sample_count, 0.0)
  {
  }

  void record_message(
    const timestamp_t receive_watchdog_time,
    const timestamp_t receive_timestamp,
    const timestamp_t acquisition_timestamp) noexcept
  {
    if (m_has_last_receive && receive_timestamp < m_last_receive_timestamp) {
      add_issue(m_transient_issues, topic_timing_issue_e::receive_time_regression);
    }
    if (m_has_last_acquisition) {
      if (acquisition_timestamp < m_last_acquisition_timestamp) {
        add_issue(m_transient_issues, topic_timing_issue_e::acquisition_time_regression);
      } else if (acquisition_timestamp == m_last_acquisition_timestamp) {
        add_issue(m_transient_issues, topic_timing_issue_e::duplicate_acquisition_time);
      } else {
        this->push_interval(
          duration_t(acquisition_timestamp - m_last_acquisition_timestamp).count());
      }
    }
    if (acquisition_timestamp > receive_timestamp) {
      add_issue(m_transient_issues, topic_timing_issue_e::future_acquisition_time);
      add_issue(m_transient_issues, topic_timing_issue_e::negative_transport_delay);
    } else {
      this->push_delay(duration_t(receive_timestamp - acquisition_timestamp).count());
    }
    if (m_has_last_receive && receive_watchdog_time < m_last_receive_watchdog_time) {
      add_issue(m_transient_issues, topic_timing_issue_e::watchdog_time_regression);
      m_start_watchdog_time = receive_watchdog_time;
    }
    m_last_receive_watchdog_time = receive_watchdog_time;
    m_last_receive_timestamp = receive_timestamp;
    m_last_acquisition_timestamp = acquisition_timestamp;
    m_has_last_receive = true;
    m_has_last_acquisition = true;
    ++m_total_sample_count;
  }

  void record_dds_issue(const topic_timing_issue_e issue) noexcept
  {
    const common::uint32_t allowed =
      issue_bits(topic_timing_issue_e::dds_deadline_missed) |
      issue_bits(topic_timing_issue_e::dds_liveliness_lost) |
      issue_bits(topic_timing_issue_e::dds_message_lost) |
      issue_bits(topic_timing_issue_e::dds_incompatible_qos);
    m_transient_issues = static_cast<topic_timing_issue_e>(
      issue_bits(m_transient_issues) | (issue_bits(issue) & allowed));
  }

  topic_timing_result_s evaluate(const timestamp_t current_watchdog_time) noexcept
  {
    topic_timing_result_s result;
    result.topic_name = m_options.topic_name;
    result.sample_count = m_total_sample_count;
    result.issues = m_transient_issues;
    m_transient_issues = topic_timing_issue_e::none;

    if (!m_has_last_receive) {
      if (current_watchdog_time < m_start_watchdog_time) {
        add_issue(result.issues, topic_timing_issue_e::watchdog_time_regression);
        m_start_watchdog_time = current_watchdog_time;
      }
      const std::chrono::milliseconds startup_age =
        std::chrono::duration_cast<std::chrono::milliseconds>(
        current_watchdog_time - m_start_watchdog_time);
      if (startup_age > m_options.startup_timeout) {
        add_issue(result.issues, topic_timing_issue_e::startup_timeout);
        result.severity = topic_timing_severity_e::error;
      }
      return result;
    }

    if (current_watchdog_time < m_last_receive_watchdog_time) {
      add_issue(result.issues, topic_timing_issue_e::watchdog_time_regression);
      m_last_receive_watchdog_time = current_watchdog_time;
    }
    result.latest_message_age_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
      current_watchdog_time - m_last_receive_watchdog_time).count();
    result.measured_rate_hz = m_interval_count == 0U ? 0.0 :
      1.0 / (m_interval_sum / static_cast<common::float64_t>(m_interval_count));
    result.mean_delay_ms = m_delay_count == 0U ? 0.0 :
      1000.0 * m_delay_sum / static_cast<common::float64_t>(m_delay_count);
    const common::float64_t mean_delay_seconds =
      m_delay_count == 0U ? 0.0 :
      m_delay_sum / static_cast<common::float64_t>(m_delay_count);
    const common::float64_t delay_variance = m_delay_count == 0U ? 0.0 :
      std::max(0.0, m_delay_square_sum / static_cast<common::float64_t>(m_delay_count) -
      mean_delay_seconds * mean_delay_seconds);
    result.delay_jitter_ms = 1000.0 * std::sqrt(delay_variance);

    topic_timing_severity_e severity =
      m_total_sample_count < m_options.startup_sample_count ?
      topic_timing_severity_e::initializing : topic_timing_severity_e::healthy;
    if (m_total_sample_count < m_options.startup_sample_count &&
      current_watchdog_time - m_start_watchdog_time > m_options.startup_timeout)
    {
      add_issue(result.issues, topic_timing_issue_e::startup_timeout);
      severity = topic_timing_severity_e::error;
    }
    this->evaluate_timeout(result, severity);
    if (m_total_sample_count >= m_options.startup_sample_count) {
      this->evaluate_statistics(result, severity);
    }
    this->evaluate_event_issues(result.issues, severity);
    result.severity = severity;
    return result;
  }

private:
  static topic_timing_options_s validate(const topic_timing_options_s & options)
  {
    if (options.topic_name.empty() || options.window_sample_count < 2U ||
      options.startup_sample_count < 2U ||
      options.startup_sample_count > options.window_sample_count ||
      options.expected_rate_hz <= 0.0 || options.warning_timeout.count() <= 0 ||
      options.error_timeout < options.warning_timeout)
    {
      throw std::invalid_argument("Invalid topic timing monitor configuration");
    }
    return options;
  }

  void push_delay(const common::float64_t value) noexcept
  {
    if (m_delay_count == m_delays.size()) {
      const common::float64_t old = m_delays[m_delay_index];
      m_delay_sum -= old;
      m_delay_square_sum -= old * old;
    } else {
      ++m_delay_count;
    }
    m_delays[m_delay_index] = value;
    m_delay_sum += value;
    m_delay_square_sum += value * value;
    m_delay_index = (m_delay_index + 1U) % m_delays.size();
  }

  void push_interval(const common::float64_t value) noexcept
  {
    if (m_interval_count == m_intervals.size()) {
      m_interval_sum -= m_intervals[m_interval_index];
    } else {
      ++m_interval_count;
    }
    m_intervals[m_interval_index] = value;
    m_interval_sum += value;
    m_interval_index = (m_interval_index + 1U) % m_intervals.size();
  }

  static void raise_severity(
    topic_timing_severity_e & severity,
    const topic_timing_severity_e requested) noexcept
  {
    if (static_cast<common::uint8_t>(requested) > static_cast<common::uint8_t>(severity)) {
      severity = requested;
    }
  }

  static void evaluate_event_issues(
    const topic_timing_issue_e issues,
    topic_timing_severity_e & severity) noexcept
  {
    if (has_issue(issues, topic_timing_issue_e::dds_liveliness_lost) ||
      has_issue(issues, topic_timing_issue_e::dds_incompatible_qos))
    {
      raise_severity(severity, topic_timing_severity_e::error);
    } else if (issues != topic_timing_issue_e::none) {
      raise_severity(severity, topic_timing_severity_e::warning);
    }
  }

  void evaluate_timeout(
    topic_timing_result_s & result,
    topic_timing_severity_e & severity) const noexcept
  {
    if (result.latest_message_age_ms >= m_options.error_timeout.count()) {
      add_issue(result.issues, topic_timing_issue_e::message_timeout);
      raise_severity(severity, topic_timing_severity_e::error);
    } else if (result.latest_message_age_ms >= m_options.warning_timeout.count()) {
      add_issue(result.issues, topic_timing_issue_e::message_timeout);
      raise_severity(severity, topic_timing_severity_e::warning);
    }
  }

  void evaluate_statistics(
    topic_timing_result_s & result,
    topic_timing_severity_e & severity) const noexcept
  {
    const common::float64_t rate_percent =
      100.0 * result.measured_rate_hz / m_options.expected_rate_hz;
    if (rate_percent <= m_options.rate_low_error_percent) {
      add_issue(result.issues, topic_timing_issue_e::rate_low);
      raise_severity(severity, topic_timing_severity_e::error);
    } else if (rate_percent <= m_options.rate_low_warning_percent) {
      add_issue(result.issues, topic_timing_issue_e::rate_low);
      raise_severity(severity, topic_timing_severity_e::warning);
    } else if (rate_percent >= m_options.rate_high_error_percent) {
      add_issue(result.issues, topic_timing_issue_e::rate_high);
      raise_severity(severity, topic_timing_severity_e::error);
    } else if (rate_percent >= m_options.rate_high_warning_percent) {
      add_issue(result.issues, topic_timing_issue_e::rate_high);
      raise_severity(severity, topic_timing_severity_e::warning);
    }

    const common::float64_t expected_period_ms = 1000.0 / m_options.expected_rate_hz;
    const common::float64_t mean_percent = 100.0 * result.mean_delay_ms / expected_period_ms;
    const common::float64_t jitter_percent =
      100.0 * result.delay_jitter_ms / expected_period_ms;
    this->evaluate_upper_threshold(
      mean_percent, m_options.delay_mean_warning_percent,
      m_options.delay_mean_error_percent, topic_timing_issue_e::delay_mean_high,
      result, severity);
    this->evaluate_upper_threshold(
      jitter_percent, m_options.delay_jitter_warning_percent,
      m_options.delay_jitter_error_percent, topic_timing_issue_e::delay_jitter_high,
      result, severity);
  }

  static void evaluate_upper_threshold(
    const common::float64_t value,
    const common::float64_t warning,
    const common::float64_t error,
    const topic_timing_issue_e issue,
    topic_timing_result_s & result,
    topic_timing_severity_e & severity) noexcept
  {
    if (value >= error) {
      add_issue(result.issues, issue);
      raise_severity(severity, topic_timing_severity_e::error);
    } else if (value >= warning) {
      add_issue(result.issues, issue);
      raise_severity(severity, topic_timing_severity_e::warning);
    }
  }

  const topic_timing_options_s m_options;
  timestamp_t m_start_watchdog_time;
  std::vector<common::float64_t> m_delays;
  std::vector<common::float64_t> m_intervals;
  std::size_t m_delay_index{0U};
  std::size_t m_interval_index{0U};
  std::size_t m_delay_count{0U};
  std::size_t m_interval_count{0U};
  common::float64_t m_delay_sum{0.0};
  common::float64_t m_delay_square_sum{0.0};
  common::float64_t m_interval_sum{0.0};
  timestamp_t m_last_receive_watchdog_time{0};
  timestamp_t m_last_receive_timestamp{0};
  timestamp_t m_last_acquisition_timestamp{0};
  common::uint32_t m_total_sample_count{0U};
  topic_timing_issue_e m_transient_issues{topic_timing_issue_e::none};
  bool m_has_last_receive{false};
  bool m_has_last_acquisition{false};
};

class topic_timing_alert_queue_c::implementation_c final
{
public:
  explicit implementation_c(const std::size_t capacity)
  : m_mutex(base_core::sync::priority_inheritance_e::disabled),
    m_alerts(validate_capacity(capacity)),
    m_severity_counts{}
  {
  }

  bool try_push(const topic_timing_alert_s & alert)
  {
    const std::lock_guard<base_core::sync::mutex_c> lock(m_mutex);
    const std::size_t incoming_severity = severity_index(alert.result.severity);
    if (m_alerts.size() == m_alerts.capacity() &&
      incoming_severity < this->highest_severity())
    {
      return false;
    }
    if (m_alerts.size() == m_alerts.capacity()) {
      const std::optional<topic_timing_alert_s> removed = m_alerts.pop_front();
      if (removed.has_value()) {
        --m_severity_counts[severity_index(removed->result.severity)];
      }
    }
    static_cast<void>(m_alerts.push_back(alert));
    ++m_severity_counts[incoming_severity];
    return true;
  }

  bool try_pop(topic_timing_alert_s & alert)
  {
    const std::lock_guard<base_core::sync::mutex_c> lock(m_mutex);
    const std::optional<topic_timing_alert_s> pending = m_alerts.pop_front();
    if (!pending.has_value()) {
      return false;
    }
    alert = *pending;
    --m_severity_counts[severity_index(alert.result.severity)];
    return true;
  }

  void clear()
  {
    const std::lock_guard<base_core::sync::mutex_c> lock(m_mutex);
    m_alerts.clear();
    m_severity_counts.fill(0U);
  }

private:
  static std::size_t validate_capacity(const std::size_t capacity)
  {
    if (capacity == 0U) {
      throw std::invalid_argument("Topic timing alert queue capacity must be positive");
    }
    return capacity;
  }

  static std::size_t severity_index(const topic_timing_severity_e severity) noexcept
  {
    return static_cast<std::size_t>(severity);
  }

  std::size_t highest_severity() const noexcept
  {
    for (std::size_t index = m_severity_counts.size(); index > 0U; --index) {
      if (m_severity_counts[index - 1U] > 0U) {
        return index - 1U;
      }
    }
    return 0U;
  }

  base_core::sync::mutex_c m_mutex;
  base_core::ring_buffer_c<topic_timing_alert_s> m_alerts;
  std::array<std::size_t, 5U> m_severity_counts;
};

topic_timing_monitor_c::topic_timing_monitor_c(
  const topic_timing_options_s & options,
  const timestamp_t start_watchdog_time)
: m_implementation(std::make_unique<implementation_c>(options, start_watchdog_time))
{
}

topic_timing_monitor_c::~topic_timing_monitor_c() noexcept = default;

topic_timing_alert_queue_c::topic_timing_alert_queue_c(const std::size_t capacity)
: m_implementation(std::make_unique<implementation_c>(capacity))
{
}

topic_timing_alert_queue_c::~topic_timing_alert_queue_c() noexcept = default;

bool topic_timing_alert_queue_c::try_push(const topic_timing_alert_s & alert)
{
  return m_implementation->try_push(alert);
}

bool topic_timing_alert_queue_c::try_pop(topic_timing_alert_s & alert)
{
  return m_implementation->try_pop(alert);
}

void topic_timing_alert_queue_c::clear()
{
  m_implementation->clear();
}

void topic_timing_monitor_c::record_message(
  const timestamp_t receive_watchdog_time,
  const timestamp_t receive_timestamp,
  const timestamp_t acquisition_timestamp) noexcept
{
  m_implementation->record_message(
    receive_watchdog_time, receive_timestamp, acquisition_timestamp);
}

void topic_timing_monitor_c::record_dds_issue(const topic_timing_issue_e issue) noexcept
{
  m_implementation->record_dds_issue(issue);
}

topic_timing_result_s topic_timing_monitor_c::evaluate(
  const timestamp_t current_watchdog_time) noexcept
{
  return m_implementation->evaluate(current_watchdog_time);
}

}  // namespace utils
