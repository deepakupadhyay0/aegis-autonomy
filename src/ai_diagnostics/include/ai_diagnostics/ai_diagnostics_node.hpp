#pragma once

#include "ai_diagnostics/ai_diagnostics_config.hpp"
#include "ai_diagnostics/diagnostic_types.hpp"
#include "ai_diagnostics/llm_client.hpp"
#include "ai_diagnostics/log_reader.hpp"
#include "ai_diagnostics/resource_monitor.hpp"
#include "ai_diagnostics/visibility_control.hpp"
#include "base_core/base_node.hpp"
#include "common/bounded_mpsc_queue.hpp"

#include <autonomy_msgs/msg/ai_diagnostic_report.hpp>
#include <diagnostic_msgs/msg/diagnostic_array.hpp>
#include <rclcpp/rclcpp.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace ai_diagnostics
{

class AI_DIAGNOSTICS_PUBLIC ai_diagnostics_node_c final
  : public base_core::ros_base_node_c
{
public:
  ai_diagnostics_node_c(
    const std::vector<std::string> & args,
    const base_core::base_node_options_s & options);
  ai_diagnostics_node_c(
    const std::vector<std::string> & args,
    const base_core::base_node_options_s & options,
    const ai_diagnostics_options_s & diagnostics_options,
    std::unique_ptr<resource_monitor_c> resource_monitor,
    std::unique_ptr<llm_client_c> llm_client);
  ~ai_diagnostics_node_c() noexcept override;

  ai_diagnostics_node_c(const ai_diagnostics_node_c &) = delete;
  ai_diagnostics_node_c & operator=(const ai_diagnostics_node_c &) = delete;
  ai_diagnostics_node_c(ai_diagnostics_node_c &&) = delete;
  ai_diagnostics_node_c & operator=(ai_diagnostics_node_c &&) = delete;

protected:
  void step1_allocate_resources(const std::vector<std::string> & args) override;
  void step2_start_threads(const std::vector<std::string> & args) override;
  void step3_run_forever(const std::vector<std::string> & args) override;

private:
  void receive_diagnostics(
    diagnostic_msgs::msg::DiagnosticArray::UniquePtr message) noexcept;
  void publish_report(
    const diagnostic_batch_s & batch,
    const diagnostic_report_s & report);
  diagnostic_batch_s collect_batch();

  ai_diagnostics_options_s m_options;
  std::unique_ptr<common::bounded_mpsc_queue_c<diagnostic_event_s>> m_queue;
  std::unique_ptr<log_reader_c> m_log_reader;
  std::unique_ptr<resource_monitor_c> m_resource_monitor;
  std::unique_ptr<llm_client_c> m_llm_client;
  std::jthread m_shutdown_watcher;
  rclcpp::Subscription<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr
    m_diagnostic_subscription;
  rclcpp::Publisher<autonomy_msgs::msg::AiDiagnosticReport>::SharedPtr
    m_report_publisher;
  std::chrono::steady_clock::time_point m_last_analysis;
  std::chrono::steady_clock::time_point m_last_queued_at;
  diagnostic_event_s m_last_queued_event;
  common::uint64_t m_report_sequence;
  std::atomic<common::uint64_t> m_input_sequence;
  std::atomic<common::uint64_t> m_rejected_event_count;
  std::atomic<common::uint64_t> m_failed_analysis_count;
  bool m_has_last_queued_event;
};

}  // namespace ai_diagnostics
