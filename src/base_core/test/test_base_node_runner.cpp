#include <gtest/gtest.h>

#include "base_core/base_node_runner.hpp"

#include <rclcpp/rclcpp.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace
{

static_assert(
  std::is_base_of_v<
    rclcpp_lifecycle::LifecycleNode,
    base_core::lifecycle_base_node_c>);

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

class lifecycle_runner_test_node_c final :
  public base_core::lifecycle_base_node_c
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
    base_core::ros_base_node_c::create_and_execute_class<runner_test_node_c>(
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
    base_core::lifecycle_base_node_c::create_and_execute_class<
    lifecycle_runner_test_node_c>(
    0,
    nullptr,
    options);

  EXPECT_EQ(result, 0);
}
