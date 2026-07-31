#pragma once

#include "base_core/base_node.hpp"
#include "base_core/execution/thread_name.hpp"
#include "logging/log_macros.hpp"

#include <chrono>
#include <cinttypes>
#include <stdexcept>

namespace base_core
{

template<typename node_t>
base_node_c<node_t>::base_node_c(const base_node_options_s & options)
: node_t(std::string(options.node_name.view()), options.node_options),
  m_context(this->get_node_base_interface()->get_context()),
  m_executor(options.executor_options),
  m_main_thread_scheduling(options.main_thread_scheduling),
  m_executor_enabled(options.enable_executor),
  m_node_added_to_executor(false),
  m_executor_thread(),
  m_executor_running(false)
{
  execution::set_current_thread_name(this->get_name());

  if (options.executor_options.context != m_context) {
    throw std::invalid_argument(
      "base_node_c node and executor contexts must match");
  }

  if (m_executor_enabled) {
    m_executor.add_node(this->get_node_base_interface());
    m_node_added_to_executor = true;
  }

  logging::logger_c::initialize(this->get_fully_qualified_name());
}

template<typename node_t>
base_node_c<node_t>::~base_node_c() noexcept
{
  this->stop_control_executor();
  if (m_node_added_to_executor) {
    try {
      m_executor.remove_node(this->get_node_base_interface());
    } catch (...) {
    }
    m_node_added_to_executor = false;
  }
  logging::logger_c::shutdown();
}

template<typename node_t>
void base_node_c<node_t>::execute_base_node(
  const std::vector<std::string> & args)
{
  this->step1_allocate_resources(args);

  try {
    this->start_control_executor();
    this->step2_start_threads(args);

    const execution::thread_scheduling_guard_c scheduling_guard(
      m_main_thread_scheduling);
    if (scheduling_guard.get_posix_error() != 0) {
      if (m_main_thread_scheduling.required) {
        throw std::runtime_error(
          "Failed to configure main-thread scheduling");
      }
      CORE_LOG_WARN(
        "Main-thread scheduling was not applied: POSIX error %" PRId32,
        scheduling_guard.get_posix_error());
    }

    this->step3_run_forever(args);
  } catch (...) {
    this->stop_control_executor();
    throw;
  }

  this->stop_control_executor();
}

template<typename node_t>
bool8_t base_node_c<node_t>::ok() const
{
  return rclcpp::ok(m_context);
}

template<typename node_t>
bool8_t base_node_c<node_t>::ok(
  const rclcpp::Context::SharedPtr & context) const
{
  return context != nullptr && rclcpp::ok(context);
}

template<typename node_t>
logging::logger_c & base_node_c<node_t>::get_logger() noexcept
{
  return logging::logger_c::get_logger();
}

template<typename node_t>
void base_node_c<node_t>::start_control_executor()
{
  if (!m_executor_enabled || m_executor_running.load()) {
    return;
  }

  m_executor_running.store(true);
  try {
    m_executor_thread = std::thread(
      [this]() {
        execution::set_current_thread_name("ros_executor");
        try {
          while (m_executor_running.load() && this->ok()) {
            m_executor.spin_once(std::chrono::milliseconds(100));
          }
        } catch (const std::exception & exception) {
          m_executor_running.store(false);
          CORE_LOG_ERROR(
            "Control executor stopped after an exception: %s",
            exception.what());
          try {
            m_context->shutdown("control executor exception");
          } catch (...) {
          }
        } catch (...) {
          m_executor_running.store(false);
          CORE_LOG_ERROR(
            "Control executor stopped after an unknown exception");
          try {
            m_context->shutdown("unknown control executor exception");
          } catch (...) {
          }
        }
      });
  } catch (...) {
    m_executor_running.store(false);
    throw;
  }
}

template<typename node_t>
void base_node_c<node_t>::stop_control_executor() noexcept
{
  if (!m_executor_enabled) {
    return;
  }

  m_executor_running.store(false);
  try {
    m_executor.cancel();
  } catch (...) {
  }

  if (m_executor_thread.joinable()) {
    m_executor_thread.join();
  }
}

}  // namespace base_core
