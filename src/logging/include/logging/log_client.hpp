#pragma once

#include "common/logging/log_protocol.hpp"
#include "logging/log_queue.hpp"
#include "logging/log_transport.hpp"
#include "logging/log_types.hpp"
#include "logging/visibility_control.hpp"

#include <atomic>
#include <memory>
#include <source_location>
#include <string_view>
#include <thread>

namespace logging
{

/// @brief Interface owned by the process-local logger facade.
/// @note enqueue() supports concurrent producers; shutdown() is called once by
/// the process control thread after producers have stopped.
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

class LOGGING_PUBLIC log_client_c final : public abstract_log_backend_c
{
public:
  log_client_c(
    std::string_view node_name,
    const logging_options_s & options);
  log_client_c(
    std::string_view node_name,
    const logging_options_s & options,
    std::unique_ptr<abstract_log_transport_c> transport);
  ~log_client_c() noexcept override;

  log_client_c(const log_client_c &) = delete;
  log_client_c & operator=(const log_client_c &) = delete;
  log_client_c(log_client_c &&) = delete;
  log_client_c & operator=(log_client_c &&) = delete;

  bool should_log(log_level_e level) const noexcept override;
  void enqueue(
    log_level_e level,
    std::string_view message,
    const std::source_location & location) noexcept override;
  void shutdown() noexcept override;

private:
  void sender_loop() noexcept;

  common::logging::log_registration_s m_registration;
  log_level_e m_minimum_level;
  log_queue_c m_queue;
  std::unique_ptr<abstract_log_transport_c> m_transport;
  std::thread m_sender_thread;
  std::atomic<bool> m_running;
};

}  // namespace logging
