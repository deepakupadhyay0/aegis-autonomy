#include "ai_diagnostics/ai_diagnostics_node.hpp"
#include "logging/node_logging.hpp"

#include <gtest/gtest.h>

#include <autonomy_msgs/msg/ai_diagnostic_report.hpp>
#include <diagnostic_msgs/msg/diagnostic_array.hpp>
#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <rclcpp/rclcpp.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

class available_resources_c final : public ai_diagnostics::resource_monitor_c
{
public:
  explicit available_resources_c(std::atomic<bool> & ready) noexcept
  : m_ready(ready)
  {
  }

  bool resources_available() noexcept override
  {
    return m_ready.load();
  }

private:
  std::atomic<bool> & m_ready;
};

class retrying_client_c final : public ai_diagnostics::llm_client_c
{
public:
  std::optional<ai_diagnostics::diagnostic_report_s> analyze(
    const ai_diagnostics::diagnostic_batch_s & batch) noexcept override
  {
    const common::uint32_t attempt = m_attempts.fetch_add(1U) + 1U;
    if (attempt == 1U || batch.events.size() != 2U) {
      return std::nullopt;
    }
    try {
      ai_diagnostics::diagnostic_report_s report;
      report.analysis = "The cause is unclear; inspect sensor timestamps.";
      return report;
    } catch (...) {
      return std::nullopt;
    }
  }

  void cancel() noexcept override
  {
  }

  common::uint32_t attempts() const noexcept
  {
    return m_attempts.load();
  }

private:
  std::atomic<common::uint32_t> m_attempts{0U};
};

class shutdown_guard_c final
{
public:
  explicit shutdown_guard_c(std::thread & worker) noexcept
  : m_worker(worker)
  {
  }

  ~shutdown_guard_c() noexcept
  {
    if (rclcpp::ok()) {
      rclcpp::shutdown();
    }
    if (m_worker.joinable()) {
      m_worker.join();
    }
  }

  shutdown_guard_c(const shutdown_guard_c &) = delete;
  shutdown_guard_c & operator=(const shutdown_guard_c &) = delete;
  shutdown_guard_c(shutdown_guard_c &&) = delete;
  shutdown_guard_c & operator=(shutdown_guard_c &&) = delete;

private:
  std::thread & m_worker;
};

}  // namespace

TEST(AiDiagnosticsNodeTest, RetriesBatchAndAttributesReportToObservedFault)
{
  using namespace std::chrono_literals;

  rclcpp::init(0, nullptr);
  ai_diagnostics::ai_diagnostics_options_s config;
  config.enabled = true;
  config.input_mode = ai_diagnostics::diagnostics_input_mode_e::dds;
  config.input_topic = "/test/diagnostics_input";
  config.report_topic = "/test/diagnostics_report";
  config.queue_capacity = 8U;
  config.max_records_per_batch = 8U;
  config.maximum_context_bytes = 4096U;
  config.maximum_response_bytes = 65536U;
  config.minimum_log_level = logging::log_level_e::warning;
  config.sample_interval = 10ms;
  config.analysis_cooldown = 20ms;

  base_core::base_node_options_s options;
  options.node_name = "ai_diagnostics_test";
  options.logging = std::make_shared<logging::node_logging_adapter_c>();
  std::atomic<bool> allow_analysis{false};
  std::unique_ptr<retrying_client_c> client =
    std::make_unique<retrying_client_c>();
  retrying_client_c * const client_view = client.get();
  std::shared_ptr<ai_diagnostics::ai_diagnostics_node_c> diagnostics =
    std::make_shared<ai_diagnostics::ai_diagnostics_node_c>(
    std::vector<std::string>{},
    options,
    config,
    std::make_unique<available_resources_c>(allow_analysis),
    std::move(client));
  std::shared_ptr<rclcpp::Node> observer =
    std::make_shared<rclcpp::Node>("ai_diagnostics_observer");
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr input =
    observer->create_publisher<diagnostic_msgs::msg::DiagnosticArray>(
    "/test/diagnostics_input", 10U);
  std::optional<autonomy_msgs::msg::AiDiagnosticReport> received;
  rclcpp::Subscription<autonomy_msgs::msg::AiDiagnosticReport>::SharedPtr output =
    observer->create_subscription<autonomy_msgs::msg::AiDiagnosticReport>(
    "/test/diagnostics_report",
    10U,
    [&received](autonomy_msgs::msg::AiDiagnosticReport::ConstSharedPtr message) {
      received = *message;
    });
  static_cast<void>(output);

  std::atomic<bool> node_failed{false};
  std::thread worker([&diagnostics, &node_failed]() {
      try {
        diagnostics->execute_base_node({});
      } catch (...) {
        node_failed.store(true);
        rclcpp::shutdown();
      }
    });
  const shutdown_guard_c shutdown(worker);

  const std::chrono::steady_clock::time_point discovery_deadline =
    std::chrono::steady_clock::now() + 3s;
  while (input->get_subscription_count() == 0U &&
    !node_failed.load() && std::chrono::steady_clock::now() < discovery_deadline)
  {
    std::this_thread::sleep_for(10ms);
  }
  ASSERT_FALSE(node_failed.load());
  ASSERT_GT(input->get_subscription_count(), 0U);

  diagnostic_msgs::msg::DiagnosticArray batch;
  batch.header.stamp = observer->now();
  diagnostic_msgs::msg::DiagnosticStatus camera;
  camera.level = diagnostic_msgs::msg::DiagnosticStatus::WARN;
  camera.name = "camera_node";
  camera.message = "frame stale";
  batch.status.push_back(camera);
  diagnostic_msgs::msg::DiagnosticStatus lidar;
  lidar.level = diagnostic_msgs::msg::DiagnosticStatus::ERROR;
  lidar.name = "lidar_node";
  lidar.message = "scan late";
  batch.status.push_back(lidar);
  input->publish(batch);
  std::this_thread::sleep_for(250ms);
  allow_analysis.store(true);

  const std::chrono::steady_clock::time_point report_deadline =
    std::chrono::steady_clock::now() + 3s;
  while (!received.has_value() && !node_failed.load() &&
    std::chrono::steady_clock::now() < report_deadline)
  {
    rclcpp::spin_some(observer);
    std::this_thread::sleep_for(10ms);
  }
  ASSERT_FALSE(node_failed.load());
  ASSERT_TRUE(received.has_value());
  EXPECT_GE(client_view->attempts(), 2U);
  EXPECT_EQ(received->source_node, "lidar_node");
  EXPECT_EQ(received->source_evidence_id, "dds:2");
  EXPECT_EQ(received->fault, "scan late");
  EXPECT_EQ(received->analysis,
    "The cause is unclear; inspect sensor timestamps.");
}
