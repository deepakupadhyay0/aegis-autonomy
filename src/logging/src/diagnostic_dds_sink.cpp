#include "logging/diagnostic_dds_sink.hpp"

#include <diagnostic_msgs/msg/diagnostic_array.hpp>
#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <diagnostic_msgs/msg/key_value.hpp>
#include <rclcpp/rclcpp.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace logging
{

namespace
{

constexpr std::size_t MAX_DIAGNOSTIC_BATCH_SIZE = 32U;
constexpr std::chrono::milliseconds DIAGNOSTIC_FLUSH_INTERVAL{50};

uint8_t to_diagnostic_level(
  const common::logging::log_level_e level) noexcept
{
  switch (level) {
    case common::logging::log_level_e::debug:
    case common::logging::log_level_e::info:
      return diagnostic_msgs::msg::DiagnosticStatus::OK;
    case common::logging::log_level_e::warning:
      return diagnostic_msgs::msg::DiagnosticStatus::WARN;
    case common::logging::log_level_e::error:
    case common::logging::log_level_e::fatal:
      return diagnostic_msgs::msg::DiagnosticStatus::ERROR;
    default:
      return diagnostic_msgs::msg::DiagnosticStatus::STALE;
  }
}

}  // namespace

class diagnostic_dds_sink_impl_c final
{
public:
  diagnostic_dds_sink_impl_c()
  : m_node(std::make_shared<rclcpp::Node>("logging_service_diagnostics")),
    m_publisher(m_node->create_publisher<
        diagnostic_msgs::msg::DiagnosticArray>("/diagnostics", 10U)),
    m_batch(),
    m_last_flush(std::chrono::steady_clock::now())
  {
    m_batch.status.reserve(MAX_DIAGNOSTIC_BATCH_SIZE);
  }

  void add(
    const std::string_view node_name,
    const common::logging::log_record_s & record)
  {
    diagnostic_msgs::msg::DiagnosticStatus status;
    status.level = to_diagnostic_level(record.level);
    status.name.assign(node_name);
    status.message.assign(record.message.view());
    status.hardware_id = "logging_service";
    status.values.reserve(3U);

    diagnostic_msgs::msg::KeyValue thread_name;
    thread_name.key = "thread_name";
    thread_name.value.assign(record.thread_name.view());
    status.values.emplace_back(std::move(thread_name));

    diagnostic_msgs::msg::KeyValue source_file;
    source_file.key = "source_file";
    source_file.value.assign(record.source_file.view());
    status.values.emplace_back(std::move(source_file));

    diagnostic_msgs::msg::KeyValue source_line;
    source_line.key = "source_line";
    source_line.value = std::to_string(record.source_line);
    status.values.emplace_back(std::move(source_line));

    m_batch.status.emplace_back(std::move(status));
    if (m_batch.status.size() >= MAX_DIAGNOSTIC_BATCH_SIZE) {
      this->flush();
    }
  }

  void flush_if_due()
  {
    if (!m_batch.status.empty() &&
      std::chrono::steady_clock::now() - m_last_flush >=
      DIAGNOSTIC_FLUSH_INTERVAL)
    {
      this->flush();
    }
  }

  void flush()
  {
    if (m_batch.status.empty()) {
      return;
    }
    m_batch.header.stamp = m_node->now();
    m_publisher->publish(m_batch);
    m_batch.status.clear();
    m_last_flush = std::chrono::steady_clock::now();
  }

private:
  rclcpp::Node::SharedPtr m_node;
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr m_publisher;
  diagnostic_msgs::msg::DiagnosticArray m_batch;
  std::chrono::steady_clock::time_point m_last_flush;
};

diagnostic_dds_sink_c::diagnostic_dds_sink_c()
: m_impl(std::make_unique<diagnostic_dds_sink_impl_c>())
{
}

diagnostic_dds_sink_c::~diagnostic_dds_sink_c() noexcept
{
  this->flush();
}

void diagnostic_dds_sink_c::add(
  const std::string_view node_name,
  const common::logging::log_record_s & record) noexcept
{
  try {
    m_impl->add(node_name, record);
  } catch (...) {
  }
}

void diagnostic_dds_sink_c::flush_if_due() noexcept
{
  try {
    m_impl->flush_if_due();
  } catch (...) {
  }
}

void diagnostic_dds_sink_c::flush() noexcept
{
  if (m_impl == nullptr) {
    return;
  }
  try {
    m_impl->flush();
  } catch (...) {
  }
}

}  // namespace logging
