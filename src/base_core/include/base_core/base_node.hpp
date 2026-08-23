#pragma once

#include "base_core/execution/thread_scheduling.hpp"
#include "base_core/node_logging.hpp"
#include "base_core/visibility_control.hpp"
#include "common/fixed_string.hpp"

#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

namespace base_core
{

struct base_node_options_s
{
  common::string64_t node_name;
  rclcpp::NodeOptions node_options;
  rclcpp::ExecutorOptions executor_options;
  execution::thread_scheduling_options_s main_thread_scheduling;
  std::shared_ptr<observability::node_logging_c> logging;
  bool8_t enable_executor{true};
};

template<typename node_t>
class base_node_c : public node_t
{
  static_assert(
    std::is_same_v<node_t, rclcpp::Node>||
    std::is_same_v<node_t, rclcpp_lifecycle::LifecycleNode>,
    "base_node_c supports rclcpp::Node and "
    "rclcpp_lifecycle::LifecycleNode");

public:
  explicit base_node_c(const base_node_options_s & options);
  ~base_node_c() noexcept override;

  base_node_c(const base_node_c<node_t> &) = delete;
  base_node_c<node_t> & operator=(const base_node_c<node_t> &) = delete;
  base_node_c(base_node_c<node_t> &&) = delete;
  base_node_c<node_t> & operator=(base_node_c<node_t> &&) = delete;

  /// Runs initialization at normal priority, then the main processing loop
  /// using the configured main-thread scheduler.
  void execute_base_node(const std::vector<std::string> & args);

  bool8_t ok() const;
  bool8_t ok(const rclcpp::Context::SharedPtr & context) const;

protected:
  virtual void step1_allocate_resources(const std::vector<std::string> & args) = 0;
  virtual void step2_start_threads(const std::vector<std::string> & args) = 0;
  virtual void step3_run_forever(const std::vector<std::string> & args) = 0;

private:
  void report(
    observability::log_level_e level,
    std::string_view message,
    const std::source_location & location =
    std::source_location::current()) const noexcept;
  void start_control_executor();
  void stop_control_executor() noexcept;

  std::shared_ptr<observability::node_logging_c> m_logging;
  rclcpp::Context::SharedPtr m_context;
  rclcpp::executors::SingleThreadedExecutor m_executor;
  execution::thread_scheduling_options_s m_main_thread_scheduling;
  bool8_t m_executor_enabled;
  bool8_t m_node_added_to_executor;
  /// Services ROS callbacks independently from the main processing loop.
  std::thread m_executor_thread;
  std::atomic<bool8_t> m_executor_running;
  bool8_t m_logging_initialized;
};

using ros_base_node_c = base_node_c<rclcpp::Node>;
using lifecycle_base_node_c =
  base_node_c<rclcpp_lifecycle::LifecycleNode>;

}  // namespace base_core

#include "base_core/base_node_impl.hpp"
