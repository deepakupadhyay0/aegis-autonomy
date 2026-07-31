#include "logging/log_client.hpp"

#include "logging/log_transport.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <pthread.h>
#include <sched.h>
#include <stdexcept>
#include <string_view>
#include <time.h>
#include <utility>

namespace logging
{

namespace
{

constexpr std::chrono::milliseconds RECONNECT_INTERVAL{100};

common::string16_t get_current_thread_name() noexcept
{
  thread_local common::string16_t thread_name;
  thread_local bool initialized = false;
  if (initialized) {
    return thread_name;
  }

  std::array<char, common::string16_t::capacity()> name{};
  if (::pthread_getname_np(::pthread_self(), name.data(), name.size()) == 0) {
    thread_name.assign(name.data());
  }
  if (thread_name.empty()) {
    thread_name = "unnamed";
  }
  initialized = true;
  return thread_name;
}

std::string_view get_source_basename(const std::string_view path) noexcept
{
  const std::size_t separator = path.find_last_of("/\\");
  return separator == std::string_view::npos ?
    path :
    path.substr(separator + 1U);
}

uint64_t get_realtime_ns() noexcept
{
  timespec current_time{};
  if (::clock_gettime(CLOCK_REALTIME, &current_time) != 0) {
    return 0U;
  }
  return
    static_cast<uint64_t>(current_time.tv_sec) * 1000000000U +
    static_cast<uint64_t>(current_time.tv_nsec);
}

void configure_sender_thread() noexcept
{
  static_cast<void>(::pthread_setname_np(::pthread_self(), "log_sender"));
  sched_param parameters{};
  parameters.sched_priority = 0;
  static_cast<void>(
    ::pthread_setschedparam(::pthread_self(), SCHED_OTHER, &parameters));
}

}  // namespace

log_client_c::log_client_c(
  const std::string_view node_name,
  const logging_options_s & options)
: log_client_c(
    node_name,
    options,
    std::make_unique<unix_log_transport_c>())
{
}

log_client_c::log_client_c(
  const std::string_view node_name,
  const logging_options_s & options,
  std::unique_ptr<abstract_log_transport_c> transport)
: m_registration(),
  m_minimum_level(options.minimum_level),
  m_queue(static_cast<std::size_t>(options.queue_capacity)),
  m_transport(std::move(transport)),
  m_sender_thread(),
  m_running(true)
{
  m_registration.node_name.assign(node_name);
  m_registration.enable_console_log = options.enable_console_log;
  if (m_registration.node_name.empty()) {
    throw std::invalid_argument("Logger node name cannot be empty");
  }
  if (m_minimum_level < log_level_e::debug ||
    m_minimum_level > log_level_e::fatal)
  {
    throw std::invalid_argument("Logger minimum level is invalid");
  }
  if (m_transport == nullptr) {
    throw std::invalid_argument("Logger transport cannot be null");
  }

  m_sender_thread = std::thread(&log_client_c::sender_loop, this);
}

log_client_c::~log_client_c() noexcept
{
  this->shutdown();
}

bool log_client_c::should_log(const log_level_e level) const noexcept
{
  return level >= m_minimum_level && level <= log_level_e::fatal;
}

void log_client_c::enqueue(
  const log_level_e level,
  const std::string_view message,
  const std::source_location & location) noexcept
{
  if (!this->should_log(level)) {
    return;
  }

  common::logging::log_record_s record;
  record.timestamp_ns = get_realtime_ns();
  record.level = level;
  record.thread_name = get_current_thread_name();
  record.source_file.assign(get_source_basename(location.file_name()));
  record.source_line = static_cast<uint32_t>(location.line());
  record.message.assign(message);
  static_cast<void>(m_queue.try_push(std::move(record)));
}

void log_client_c::shutdown() noexcept
{
  const bool was_running = m_running.exchange(false, std::memory_order_acq_rel);
  if (!was_running) {
    return;
  }
  m_queue.shutdown();
  if (m_sender_thread.joinable()) {
    m_sender_thread.join();
  }
}

void log_client_c::sender_loop() noexcept
{
  configure_sender_thread();
  bool transport_connected = false;
  std::optional<common::logging::log_record_s> pending_record;

  while (true) {
    if (!pending_record.has_value()) {
      common::logging::log_record_s record;
      if (!m_queue.wait_and_pop(record)) {
        break;
      }
      pending_record.emplace(std::move(record));
    }

    if (!transport_connected) {
      if (!m_running.load(std::memory_order_acquire)) {
        break;
      }
      transport_connected = m_transport->connect(m_registration);
      if (!transport_connected) {
        m_transport->disconnect();
        std::this_thread::sleep_for(RECONNECT_INTERVAL);
        continue;
      }
    }

    pending_record->dropped_record_count =
      m_queue.get_dropped_record_count();
    if (m_transport->send(pending_record.value())) {
      pending_record.reset();
      continue;
    }

    m_transport->disconnect();
    transport_connected = false;
    if (!m_running.load(std::memory_order_acquire)) {
      break;
    }
    std::this_thread::sleep_for(RECONNECT_INTERVAL);
  }

  m_transport->disconnect();
}

}  // namespace logging
