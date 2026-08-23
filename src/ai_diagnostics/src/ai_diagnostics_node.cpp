#include "ai_diagnostics/ai_diagnostics_node.hpp"

#include "logging/log_macros.hpp"

#include <diagnostic_msgs/msg/diagnostic_status.hpp>

#include <charconv>
#include <chrono>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace ai_diagnostics
{
namespace
{

common::uint32_t parse_source_line(
  const diagnostic_msgs::msg::DiagnosticStatus & status) noexcept
{
  for (const diagnostic_msgs::msg::KeyValue & value : status.values) {
    if (value.key == "source_line") {
      common::uint32_t source_line = 0U;
      const std::from_chars_result result = std::from_chars(
        value.value.data(),
        value.value.data() + value.value.size(),
        source_line);
      return result.ec == std::errc{} ? source_line : 0U;
    }
  }
  return 0U;
}

std::string_view find_value(
  const diagnostic_msgs::msg::DiagnosticStatus & status,
  const std::string_view key) noexcept
{
  for (const diagnostic_msgs::msg::KeyValue & value : status.values) {
    if (value.key == key) {
      return value.value;
    }
  }
  return {};
}

bool events_match(
  const diagnostic_event_s & left,
  const diagnostic_event_s & right) noexcept
{
  return left.level == right.level &&
         left.source_line == right.source_line &&
         left.source_node == right.source_node &&
         left.source_file == right.source_file &&
         left.fault == right.fault;
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
    std::this_thread::sleep_for(m_options.sample_interval);
    if (!m_options.enabled || m_resource_monitor == nullptr ||
      m_llm_client == nullptr ||
      (m_queue == nullptr && m_log_reader == nullptr))
    {
      continue;
    }
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

    diagnostic_batch_s batch = this->collect_batch();
    if (batch.events.empty()) {
      continue;
    }
    m_last_analysis = now;

    const std::optional<diagnostic_report_s> report =
      m_llm_client->analyze(batch);
    if (!report.has_value()) {
      m_failed_analysis_count.fetch_add(1U, std::memory_order_relaxed);
      CORE_LOG_WARN_THROTTLE(
        30s,
        "AI diagnostic analysis failed; event was not republished");
      continue;
    }
    if (report->insufficient_evidence) {
      continue;
    }
    this->publish_report(batch, report.value());
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
    diagnostic_event_s event;
    while (batch.events.size() < m_options.max_records_per_batch &&
      m_queue->try_pop(event))
    {
      batch.events.push_back(event);
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

  const std::string_view node_name(this->get_fully_qualified_name());
  for (const diagnostic_msgs::msg::DiagnosticStatus & status :
    message->status)
  {
    const logging::log_level_e log_level = to_log_level(status.level);
    if (log_level < m_options.minimum_log_level ||
      status.name == node_name || status.name == this->get_name())
    {
      continue;
    }

    diagnostic_event_s event;
    event.timestamp = std::to_string(message->header.stamp.sec) + "." +
      std::to_string(message->header.stamp.nanosec);
    const common::uint64_t input_sequence =
      m_input_sequence.fetch_add(1U, std::memory_order_relaxed) + 1U;
    event.evidence_id = std::string("dds:") +
      std::to_string(input_sequence);
    event.level = static_cast<common::uint8_t>(log_level);
    event.source_line = parse_source_line(status);
    event.source_node.assign(status.name);
    event.source_file.assign(find_value(status, "source_file"));
    event.fault.assign(status.message);
    const std::chrono::steady_clock::time_point now =
      std::chrono::steady_clock::now();
    if (m_has_last_queued_event &&
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
}

void ai_diagnostics_node_c::publish_report(
  const diagnostic_batch_s & batch,
  const diagnostic_report_s & report)
{
  if (m_report_publisher == nullptr || batch.events.empty()) {
    return;
  }

  const diagnostic_event_s & event = batch.events.back();

  autonomy_msgs::msg::AiDiagnosticReport message;
  message.header.stamp = this->now();
  message.sequence = ++m_report_sequence;
  message.source_level = event.level;
  message.source_node.assign(event.source_node.view());
  message.fault.assign(event.fault.view());
  message.probable_cause.assign(report.probable_cause.view());
  message.predicted_failure.assign(report.predicted_failure.view());
  message.recommended_action.assign(report.recommended_action.view());
  message.evidence_ids.reserve(report.evidence_ids.size());
  for (const common::string128_t & evidence_id : report.evidence_ids) {
    message.evidence_ids.emplace_back(evidence_id.view());
  }
  message.confidence = report.confidence;
  message.insufficient_evidence = report.insufficient_evidence;
  message.potentially_recoverable = report.potentially_recoverable;
  m_report_publisher->publish(message);
}

}  // namespace ai_diagnostics
