#include <gtest/gtest.h>

#include "base_core/base_node_runner.hpp"

#include <rclcpp/rclcpp.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

namespace
{

static_assert(
  std::is_base_of_v<
    rclcpp_lifecycle::LifecycleNode,
    base_core::lifecycle_base_node_c>);

class test_node_logging_c final
  : public base_core::observability::node_logging_c
{
public:
  test_node_logging_c() noexcept
  : m_initialize_count(0U),
    m_shutdown_count(0U),
    m_initialized(false)
  {
  }

  void initialize(const std::string_view node_name) override
  {
    static_cast<void>(node_name);
    m_initialize_count.fetch_add(1U);
    m_initialized.store(true);
  }

  void shutdown() noexcept override
  {
    if (m_initialized.exchange(false)) {
      m_shutdown_count.fetch_add(1U);
    }
  }

  bool is_initialized() const noexcept override
  {
    return m_initialized.load();
  }

  void write(
    const base_core::observability::log_level_e level,
    const std::string_view message,
    const std::source_location & location) noexcept override
  {
    static_cast<void>(level);
    static_cast<void>(message);
    static_cast<void>(location);
  }

  common::uint32_t get_initialize_count() const noexcept
  {
    return m_initialize_count.load();
  }

  common::uint32_t get_shutdown_count() const noexcept
  {
    return m_shutdown_count.load();
  }

private:
  std::atomic<common::uint32_t> m_initialize_count;
  std::atomic<common::uint32_t> m_shutdown_count;
  std::atomic<bool8_t> m_initialized;
};

class runner_test_node_c final : public base_core::ros_base_node_c
{
public:
  runner_test_node_c(
    const std::vector<std::string> & args,
    const base_core::base_node_options_s & options)
  : base_core::ros_base_node_c(options),
    m_timer()
  {
    static_cast<void>(args);
    s_timer_called.store(false);
  }

  static bool8_t timer_was_called() noexcept
  {
    return s_timer_called.load();
  }

protected:
  void step1_allocate_resources(
    const std::vector<std::string> & args) override
  {
    static_cast<void>(args);
    m_timer = this->create_wall_timer(
      std::chrono::milliseconds(1),
      []() {
        s_timer_called.store(true);
      });
  }

  void step2_start_threads(const std::vector<std::string> & args) override
  {
    static_cast<void>(args);
  }

  void step3_run_forever(const std::vector<std::string> & args) override
  {
    static_cast<void>(args);
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!s_timer_called.load() &&
      std::chrono::steady_clock::now() < deadline)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (!s_timer_called.load()) {
      throw std::runtime_error("base control executor did not service timer");
    }
  }

private:
  rclcpp::TimerBase::SharedPtr m_timer;
  inline static std::atomic<bool8_t> s_timer_called{false};
};

class lifecycle_runner_test_node_c final
  : public base_core::lifecycle_base_node_c
{
public:
  lifecycle_runner_test_node_c(
    const std::vector<std::string> & args,
    const base_core::base_node_options_s & options)
  : base_core::lifecycle_base_node_c(options)
  {
    static_cast<void>(args);
  }

protected:
  void step1_allocate_resources(
    const std::vector<std::string> & args) override
  {
    static_cast<void>(args);
  }

  void step2_start_threads(const std::vector<std::string> & args) override
  {
    static_cast<void>(args);
  }

  void step3_run_forever(const std::vector<std::string> & args) override
  {
    static_cast<void>(args);
  }
};

}  // namespace

TEST(BaseNodeRunnerTest, ServicesControlPlaneAndStopsCleanly)
{
  base_core::base_node_options_s options;
  options.node_name = "base_node_runner_test";

  const int32_t result =
    base_core::create_and_execute_node<runner_test_node_c>(
    0,
    nullptr,
    options);

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(runner_test_node_c::timer_was_called());
}

TEST(BaseNodeRunnerTest, SupportsLifecycleNodeBackend)
{
  base_core::base_node_options_s options;
  options.node_name = "lifecycle_base_node_runner_test";

  const int32_t result =
    base_core::create_and_execute_node<lifecycle_runner_test_node_c>(
    0,
    nullptr,
    options);

  EXPECT_EQ(result, 0);
}

TEST(BaseNodeRunnerTest, InitializesOnlyAnInjectedLoggingService)
{
  base_core::base_node_options_s options;
  options.node_name = "base_node_logging_test";
  std::shared_ptr<test_node_logging_c> logging =
    std::make_shared<test_node_logging_c>();
  options.logging = logging;

  const int32_t result =
    base_core::create_and_execute_node<lifecycle_runner_test_node_c>(
    0,
    nullptr,
    options);

  EXPECT_EQ(result, 0);
  EXPECT_EQ(logging->get_initialize_count(), 1U);
  EXPECT_EQ(logging->get_shutdown_count(), 1U);
}
