#pragma once

#include "logging/log_queue.hpp"
#include "logging/log_types.hpp"
#include "logging/visibility_control.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <source_location>
#include <stop_token>
#include <string_view>
#include <thread>

namespace logging
{

class abstract_log_sink_c;
class diagnostic_dds_sink_c;

/// Interface owned by the process-local logger facade.
/// enqueue() supports concurrent producers, never waits for the writer, and
/// expects the facade to apply normal severity filtering.
class LOGGING_PUBLIC abstract_log_backend_c
{
public:
  virtual ~abstract_log_backend_c() noexcept = default;

  abstract_log_backend_c(const abstract_log_backend_c &) = delete;
  abstract_log_backend_c & operator=(const abstract_log_backend_c &) = delete;
  abstract_log_backend_c(abstract_log_backend_c &&) = delete;
  abstract_log_backend_c & operator=(abstract_log_backend_c &&) = delete;

  virtual bool should_log(log_level_e level) const noexcept = 0;
  virtual void enqueue(
    log_level_e level,
    std::string_view message,
    const std::source_location & location) noexcept = 0;
  virtual void shutdown() noexcept = 0;

protected:
  abstract_log_backend_c() noexcept = default;
};

/// Owns the sole bounded log queue and its low-priority writer thread.
class LOGGING_PUBLIC local_log_backend_c final : public abstract_log_backend_c
{
public:
  local_log_backend_c(
    std::string_view node_name,
    const logging_options_s & options);
  local_log_backend_c(
    std::string_view node_name,
    const logging_options_s & options,
    std::unique_ptr<abstract_log_sink_c> sink);
  ~local_log_backend_c() noexcept override;

  local_log_backend_c(const local_log_backend_c &) = delete;
  local_log_backend_c & operator=(const local_log_backend_c &) = delete;
  local_log_backend_c(local_log_backend_c &&) = delete;
  local_log_backend_c & operator=(local_log_backend_c &&) = delete;

  bool should_log(log_level_e level) const noexcept override;
  void enqueue(
    log_level_e level,
    std::string_view message,
    const std::source_location & location) noexcept override;
  void shutdown() noexcept override;

private:
  void writer_loop() noexcept;
  void diagnostic_loop(std::stop_token stop_token) noexcept;
  void flush_sink() noexcept;
  log_queue_statistics_s enqueue_queue_diagnostics(
    const log_queue_statistics_s & previous_statistics) noexcept;
  void write_drop_notice(
    const log_record_s & next_record,
    common::uint64_t dropped_record_count) noexcept;

  common::string64_t m_node_name;
  log_level_e m_minimum_level;
  std::chrono::milliseconds m_queue_diagnostic_interval;
  log_queue_c m_queue;
  std::unique_ptr<abstract_log_sink_c> m_sink;
  std::unique_ptr<diagnostic_dds_sink_c> m_diagnostic_sink;
  std::atomic<common::uint64_t> m_processed_record_count;
  std::atomic<common::uint64_t> m_sink_failure_count;
  std::atomic<common::uint64_t> m_flush_count;
  std::atomic<bool> m_running;
  std::mutex m_diagnostic_mutex;
  std::condition_variable_any m_diagnostic_condition;
  std::jthread m_diagnostic_thread;
  std::jthread m_writer_thread;
};

}  // namespace logging
