#include "ai_diagnostics/ai_diagnostics_node.hpp"

#include "logging/log_macros.hpp"

#include <autonomy_msgs/msg/ai_diagnostic_hypothesis.hpp>
#include <diagnostic_msgs/msg/diagnostic_status.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cinttypes>
#include <cstddef>
#include <cstdio>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace ai_diagnostics
{
namespace
{

constexpr std::size_t MAX_STATUS_VALUES = 32U;

common::uint32_t parse_source_line(
  const diagnostic_msgs::msg::DiagnosticStatus & status) noexcept
{
  const std::size_t limit = std::min(status.values.size(), MAX_STATUS_VALUES);
  for (std::size_t index = 0U; index < limit; ++index) {
    const diagnostic_msgs::msg::KeyValue & value = status.values[index];
    if (value.key == "source_line") {
      common::uint32_t source_line = 0U;
      const std::from_chars_result result = std::from_chars(
        value.value.data(),
        value.value.data() + value.value.size(),
        source_line);
      return result.ec == std::errc{} &&
             result.ptr == value.value.data() + value.value.size() ?
             source_line : 0U;
    }
  }
  return 0U;
}

std::string_view find_value(
  const diagnostic_msgs::msg::DiagnosticStatus & status,
  const std::string_view key) noexcept
{
  const std::size_t limit = std::min(status.values.size(), MAX_STATUS_VALUES);
  for (std::size_t index = 0U; index < limit; ++index) {
    const diagnostic_msgs::msg::KeyValue & value = status.values[index];
    if (value.key == key) {
      return value.value;
    }
  }
  return {};
}

std::size_t event_context_bytes(const diagnostic_event_s & event) noexcept
{
  constexpr std::size_t FIELD_OVERHEAD = 128U;
  std::size_t bytes = event.timestamp.size() + event.evidence_id.size() +
    event.source_node.size() + event.source_file.size() +
    event.fault.size() + FIELD_OVERHEAD;
  for (const diagnostic_measurement_s & measurement : event.measurements) {
    bytes += measurement.name.size() + measurement.value.size() + 32U;
  }
  return bytes;
}

bool events_match(
  const diagnostic_event_s & left,
  const diagnostic_event_s & right) noexcept
{
  return left.level == right.level &&
         left.source_line == right.source_line &&
         left.source_node == right.source_node &&
         left.source_file == right.source_file &&
         left.fault == right.fault &&
         left.healthy == right.healthy &&
         left.measurements.size() == right.measurements.size() &&
         std::equal(
    left.measurements.begin(), left.measurements.end(),
    right.measurements.begin(),
    [](const diagnostic_measurement_s & lhs,
    const diagnostic_measurement_s & rhs) {
      return lhs.name == rhs.name && lhs.value == rhs.value;
    });
}

logging::log_level_e to_log_level(const common::uint8_t level) noexcept
{
  if (level == diagnostic_msgs::msg::DiagnosticStatus::WARN) {
    return logging::log_level_e::warning;
  }
  if (level == diagnostic_msgs::msg::DiagnosticStatus::ERROR) {
    return logging::log_level_e::error;
  }
  if (level == diagnostic_msgs::msg::DiagnosticStatus::STALE) {
    return logging::log_level_e::fatal;
  }
  return logging::log_level_e::info;
}

}  // namespace

ai_diagnostics_node_c::ai_diagnostics_node_c(
  const std::vector<std::string> & args,
  const base_core::base_node_options_s & options)
: base_core::ros_base_node_c(options),
  m_options(load_ai_diagnostics_options()),
  m_queue(),
  m_log_reader(),
  m_resource_monitor(),
  m_llm_client(),
  m_window_builder(),
  m_ready_windows(),
  m_deferred_event(),
  m_shutdown_watcher(),
  m_diagnostic_subscription(),
  m_report_publisher(),
  m_last_analysis(std::chrono::steady_clock::time_point::min()),
  m_last_queued_at(std::chrono::steady_clock::time_point::min()),
  m_last_queued_event(),
  m_report_sequence(0U),
  m_input_sequence(0U),
  m_rejected_event_count(0U),
  m_failed_analysis_count(0U),
  m_has_last_queued_event(false)
{
  static_cast<void>(args);
}

ai_diagnostics_node_c::ai_diagnostics_node_c(
  const std::vector<std::string> & args,
  const base_core::base_node_options_s & options,
  const ai_diagnostics_options_s & diagnostics_options,
  std::unique_ptr<resource_monitor_c> resource_monitor,
  std::unique_ptr<llm_client_c> llm_client)
: base_core::ros_base_node_c(options),
  m_options(diagnostics_options),
  m_queue(),
  m_log_reader(),
  m_resource_monitor(std::move(resource_monitor)),
  m_llm_client(std::move(llm_client)),
  m_window_builder(),
  m_ready_windows(),
  m_deferred_event(),
  m_shutdown_watcher(),
  m_diagnostic_subscription(),
  m_report_publisher(),
  m_last_analysis(std::chrono::steady_clock::time_point::min()),
  m_last_queued_at(std::chrono::steady_clock::time_point::min()),
  m_last_queued_event(),
  m_report_sequence(0U),
  m_input_sequence(0U),
  m_rejected_event_count(0U),
  m_failed_analysis_count(0U),
  m_has_last_queued_event(false)
{
  static_cast<void>(args);
  if (m_options.enabled &&
    (m_resource_monitor == nullptr || m_llm_client == nullptr))
  {
    throw std::invalid_argument(
            "Enabled AI diagnostics requires injected dependencies");
  }
}

ai_diagnostics_node_c::~ai_diagnostics_node_c() noexcept
{
  m_diagnostic_subscription.reset();
  m_shutdown_watcher.request_stop();
  if (m_llm_client != nullptr) {
    m_llm_client->cancel();
  }
  if (m_shutdown_watcher.joinable()) {
    m_shutdown_watcher.join();
  }
  if (m_queue != nullptr) {
    m_queue->shutdown();
  }
}

void ai_diagnostics_node_c::step1_allocate_resources(
  const std::vector<std::string> & args)
{
  static_cast<void>(args);
  if (!m_options.enabled) {
    CORE_LOG_INFO("AI diagnostics is disabled by configuration");
    return;
  }

  if (m_options.input_mode == diagnostics_input_mode_e::dds) {
    m_queue = std::make_unique<
      common::bounded_mpsc_queue_c<diagnostic_event_s>>(
      m_options.queue_capacity);
  } else {
    m_log_reader = std::make_unique<log_reader_c>(m_options);
  }
  if (m_resource_monitor == nullptr) {
    m_resource_monitor = std::make_unique<linux_cpu_monitor_c>(
      m_options.cpu_threshold_percent,
      m_options.idle_samples_required);
  }
  if (m_llm_client == nullptr) {
    m_llm_client = std::make_unique<openai_compatible_llm_client_c>(m_options);
  }
  m_window_builder = std::make_unique<incident_window_c>(
    m_options.max_records_per_batch,
    m_options.maximum_context_bytes);

  if (m_options.input_mode == diagnostics_input_mode_e::dds) {
    const rclcpp::QoS input_qos(rclcpp::KeepLast(10U));
    m_diagnostic_subscription = this->create_subscription<
      diagnostic_msgs::msg::DiagnosticArray>(
      std::string(m_options.input_topic.view()),
      input_qos,
      [this](diagnostic_msgs::msg::DiagnosticArray::UniquePtr message) {
        this->receive_diagnostics(std::move(message));
      });
  }
  m_report_publisher = this->create_publisher<
    autonomy_msgs::msg::AiDiagnosticReport>(
    std::string(m_options.report_topic.view()),
    rclcpp::QoS(rclcpp::KeepLast(10U)));

  CORE_LOG_INFO("AI diagnostics advisory processing enabled");
}

void ai_diagnostics_node_c::step2_start_threads(
  const std::vector<std::string> & args)
{
  static_cast<void>(args);
  if (!m_options.enabled || m_llm_client == nullptr) {
    return;
  }
  m_shutdown_watcher = std::jthread(
    [this](const std::stop_token & stop_token) noexcept {
      using namespace std::chrono_literals;
      while (!stop_token.stop_requested() && this->ok()) {
        std::this_thread::sleep_for(100ms);
      }
      if (!stop_token.stop_requested() && m_llm_client != nullptr) {
        m_llm_client->cancel();
      }
    });
}

void ai_diagnostics_node_c::step3_run_forever(
  const std::vector<std::string> & args)
{
  using namespace std::chrono_literals;

  static_cast<void>(args);
  while (this->ok()) {
    std::chrono::milliseconds remaining = m_options.sample_interval;
    while (this->ok() && remaining > 0ms) {
      const std::chrono::milliseconds interval = std::min(remaining, 100ms);
      std::this_thread::sleep_for(interval);
      remaining -= interval;
    }
    if (!this->ok()) {
      break;
    }
    if (!m_options.enabled || m_resource_monitor == nullptr ||
      m_llm_client == nullptr ||
      (m_queue == nullptr && m_log_reader == nullptr))
    {
      continue;
    }
    CORE_LOG_INFO_THROTTLE(
      30s,
      "AI diagnostics: reports=%" PRIu64 " rejected=%" PRIu64
      " failed=%" PRIu64 " pending=%u",
      m_report_sequence,
      m_rejected_event_count.load(std::memory_order_relaxed),
      m_failed_analysis_count.load(std::memory_order_relaxed),
      static_cast<common::uint32_t>(m_ready_windows.size()));
    if (!m_resource_monitor->resources_available()) {
      continue;
    }

    const std::chrono::steady_clock::time_point now =
      std::chrono::steady_clock::now();
    if (m_last_analysis != std::chrono::steady_clock::time_point::min() &&
      now - m_last_analysis < m_options.analysis_cooldown)
    {
      continue;
    }

    if (m_ready_windows.empty()) {
      diagnostic_batch_s input = this->collect_batch();
      if (!input.events.empty()) {
        std::vector<diagnostic_batch_s> windows = m_window_builder->ingest(
          std::span<const diagnostic_event_s>(input.events));
        for (diagnostic_batch_s & window : windows) {
          m_ready_windows.push_back(std::move(window));
        }
      }
      if (m_ready_windows.empty()) {
        if (m_log_reader != nullptr && !m_log_reader->commit()) {
          CORE_LOG_WARN_THROTTLE(30s, "AI diagnostic cursor was not saved");
        }
        continue;
      }
    }
    m_last_analysis = now;

    const std::optional<diagnostic_report_s> report =
      m_llm_client->analyze(m_ready_windows.front());
    if (!report.has_value() || report->analysis.empty() ||
      report->analysis.size() > m_options.maximum_response_bytes)
    {
      m_failed_analysis_count.fetch_add(1U, std::memory_order_relaxed);
      CORE_LOG_WARN_THROTTLE(
        30s,
        "AI diagnostic analysis failed; retaining evidence for retry");
      continue;
    }
    if (!m_ready_windows.front().recovery_observed) {
      this->publish_report(m_ready_windows.front(), report.value());
    }
    m_ready_windows.pop_front();
    if (m_ready_windows.empty() && m_log_reader != nullptr &&
      !m_log_reader->commit())
    {
      CORE_LOG_WARN_THROTTLE(30s, "AI diagnostic cursor was not saved");
    }
  }
}

diagnostic_batch_s ai_diagnostics_node_c::collect_batch()
{
  diagnostic_batch_s batch;
  batch.events.reserve(m_options.max_records_per_batch);
  if (m_options.input_mode == diagnostics_input_mode_e::dds) {
    if (m_queue == nullptr) {
      return batch;
    }
    std::size_t context_bytes = 0U;
    while (batch.events.size() < m_options.max_records_per_batch) {
      diagnostic_event_s event;
      if (m_deferred_event.has_value()) {
        event = *m_deferred_event;
        m_deferred_event.reset();
      } else if (!m_queue->try_pop(event)) {
        break;
      }
      const std::size_t event_bytes = event_context_bytes(event);
      if (event_bytes > m_options.maximum_context_bytes) {
        m_rejected_event_count.fetch_add(1U, std::memory_order_relaxed);
        continue;
      }
      if (event_bytes > m_options.maximum_context_bytes - context_bytes) {
        m_deferred_event.emplace(event);
        break;
      }
      batch.events.push_back(event);
      context_bytes += event_bytes;
    }
    return batch;
  }

  if (m_log_reader == nullptr) {
    return batch;
  }
  const std::vector<parsed_log_record_s> records =
    m_log_reader->read_next_batch();
  for (const parsed_log_record_s & record : records) {
    if (record.node_name == this->get_name()) {
      continue;
    }
    diagnostic_event_s event;
    event.timestamp = record.timestamp;
    event.evidence_id = record.evidence_id;
    event.level = static_cast<common::uint8_t>(record.level);
    event.source_line = record.source_line;
    event.source_node = record.node_name;
    event.source_file = record.source_file;
    event.fault = record.message;
    event.truncated = record.truncated;
    event.healthy = record.level == logging::log_level_e::info &&
      record.message.view().starts_with("health=ok");
    extract_numeric_log_measurements(event);
    batch.events.push_back(event);
  }
  return batch;
}

void ai_diagnostics_node_c::receive_diagnostics(
  diagnostic_msgs::msg::DiagnosticArray::UniquePtr message) noexcept
{
  if (message == nullptr || m_queue == nullptr) {
    return;
  }

  try {
    if (message->header.stamp.sec < 0 ||
      message->header.stamp.nanosec >= 1'000'000'000U)
    {
      m_rejected_event_count.fetch_add(
        static_cast<common::uint64_t>(message->status.size()),
        std::memory_order_relaxed);
      return;
    }
    char timestamp[32]{};
    const common::int32_t written = std::snprintf(
      timestamp, sizeof(timestamp), "%010" PRId32 ".%09" PRIu32,
      message->header.stamp.sec, message->header.stamp.nanosec);
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(timestamp)) {
      m_rejected_event_count.fetch_add(
        static_cast<common::uint64_t>(message->status.size()),
        std::memory_order_relaxed);
      return;
    }
    const std::string_view node_name(this->get_fully_qualified_name());
    const std::size_t status_limit = std::min(
      message->status.size(),
      static_cast<std::size_t>(m_options.queue_capacity));
    m_rejected_event_count.fetch_add(
      static_cast<common::uint64_t>(message->status.size() - status_limit),
      std::memory_order_relaxed);
    for (std::size_t index = 0U; index < status_limit; ++index) {
      const diagnostic_msgs::msg::DiagnosticStatus & status =
        message->status[index];
      const logging::log_level_e log_level = to_log_level(status.level);
      if ((log_level < m_options.minimum_log_level &&
        status.level != diagnostic_msgs::msg::DiagnosticStatus::OK) ||
        status.name == node_name || status.name == this->get_name())
      {
        continue;
      }
      if (status.name.empty() ||
        status.name.size() > common::string64_t::capacity())
      {
        m_rejected_event_count.fetch_add(1U, std::memory_order_relaxed);
        continue;
      }

      diagnostic_event_s event;
      event.timestamp.assign(timestamp);
      const common::uint64_t input_sequence =
        m_input_sequence.fetch_add(1U, std::memory_order_relaxed) + 1U;
      event.evidence_id = std::string("dds:") +
        std::to_string(input_sequence);
      event.level = static_cast<common::uint8_t>(log_level);
      event.source_line = parse_source_line(status);
      event.source_node.assign(status.name);
      const std::string_view source_file = find_value(status, "source_file");
      event.source_file.assign(source_file);
      event.fault.assign(status.message);
      event.truncated = status.message.size() > common::string256_t::capacity() ||
        source_file.size() > common::string64_t::capacity() ||
        status.values.size() > MAX_STATUS_VALUES;
      event.healthy = status.level == diagnostic_msgs::msg::DiagnosticStatus::OK;
      const std::size_t value_limit = std::min(
        status.values.size(), MAX_STATUS_VALUES);
      for (std::size_t value_index = 0U; value_index < value_limit; ++value_index) {
        const diagnostic_msgs::msg::KeyValue & value = status.values[value_index];
        if (value.key == "source_file" || value.key == "source_line") {
          continue;
        }
        if (event.measurements.size() >= MAX_DIAGNOSTIC_MEASUREMENTS ||
          value.key.size() > common::string64_t::capacity() ||
          value.value.size() > common::string64_t::capacity())
        {
          event.truncated = true;
          continue;
        }
        diagnostic_measurement_s measurement;
        measurement.name.assign(value.key);
        measurement.value.assign(value.value);
        event.measurements.push_back(std::move(measurement));
      }
      const std::chrono::steady_clock::time_point now =
        std::chrono::steady_clock::now();
      if (!event.healthy && m_has_last_queued_event &&
        now - m_last_queued_at < m_options.analysis_cooldown &&
        events_match(event, m_last_queued_event))
      {
        continue;
      }
      m_last_queued_event = event;
      // The queue API uses an rvalue reference to make event transfer explicit.
      if (m_queue->try_push(
          std::move(event)))    // NOLINT(performance-move-const-arg)
      {
        m_last_queued_at = now;
        m_has_last_queued_event = true;
      } else {
        m_has_last_queued_event = false;
        m_rejected_event_count.fetch_add(1U, std::memory_order_relaxed);
      }
    }
  } catch (...) {
    m_rejected_event_count.fetch_add(1U, std::memory_order_relaxed);
  }
}

void ai_diagnostics_node_c::publish_report(
  const diagnostic_batch_s & batch,
  const diagnostic_report_s & report)
{
  if (m_report_publisher == nullptr || batch.events.empty()) {
    return;
  }

  const diagnostic_event_s * event = nullptr;
  for (const diagnostic_event_s & candidate : batch.events) {
    if (!candidate.healthy &&
      candidate.level >= static_cast<common::uint8_t>(
        logging::log_level_e::warning) &&
      (event == nullptr || candidate.level > event->level))
    {
      event = &candidate;
    }
  }
  if (event == nullptr) {
    return;
  }

  autonomy_msgs::msg::AiDiagnosticReport message;
  message.header.stamp = this->now();
  message.incident_id.assign(batch.incident_id.view());
  message.as_of.assign(batch.as_of.view());
  message.sequence = ++m_report_sequence;
  message.source_level = event->level;
  message.source_node.assign(event->source_node.view());
  message.source_evidence_id.assign(event->evidence_id.view());
  message.fault.assign(event->fault.view());
  message.analysis.assign(report.analysis);
  m_report_publisher->publish(message);
}

}  // namespace ai_diagnostics
