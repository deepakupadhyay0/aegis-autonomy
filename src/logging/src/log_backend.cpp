#include "logging/log_backend.hpp"

#include "logging/diagnostic_dds_sink.hpp"
#include "logging/node_log_sink.hpp"

#include <array>
#include <cinttypes>
#include <cstdio>
#include <ctime>
#include <pthread.h>
#include <sched.h>
#include <stdexcept>
#include <string_view>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <utility>

namespace logging
{

namespace
{

const common::string16_t & get_current_thread_name() noexcept
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

common::uint64_t get_realtime_ns() noexcept
{
  timespec current_time{};
  if (::clock_gettime(CLOCK_REALTIME, &current_time) != 0) {
    return 0U;
  }
  return
    static_cast<common::uint64_t>(current_time.tv_sec) * 1000000000U +
    static_cast<common::uint64_t>(current_time.tv_nsec);
}

void configure_writer_thread() noexcept
{
  static_cast<void>(::pthread_setname_np(::pthread_self(), "log_writer"));
  sched_param parameters{};
  parameters.sched_priority = 0;
  static_cast<void>(
    ::pthread_setschedparam(::pthread_self(), SCHED_OTHER, &parameters));
#ifdef SYS_gettid
  const pid_t thread_id = static_cast<pid_t>(::syscall(SYS_gettid));
  static_cast<void>(::setpriority(PRIO_PROCESS, thread_id, 10));
#endif
}

void configure_diagnostic_thread() noexcept
{
  static_cast<void>(::pthread_setname_np(::pthread_self(), "log_diag"));
  sched_param parameters{};
  parameters.sched_priority = 0;
  static_cast<void>(
    ::pthread_setschedparam(::pthread_self(), SCHED_OTHER, &parameters));
#ifdef SYS_gettid
  const pid_t thread_id = static_cast<pid_t>(::syscall(SYS_gettid));
  static_cast<void>(::setpriority(PRIO_PROCESS, thread_id, 10));
#endif
}

common::string64_t validate_node_name(const std::string_view node_name)
{
  common::string64_t validated_node_name;
  validated_node_name.assign(node_name);
  if (validated_node_name.empty()) {
    throw std::invalid_argument("Logger node name cannot be empty");
  }
  return validated_node_name;
}

log_level_e validate_log_level(const log_level_e level)
{
  if (level < log_level_e::debug || level > log_level_e::fatal) {
    throw std::invalid_argument("Logger minimum level is invalid");
  }
  return level;
}

std::chrono::milliseconds validate_diagnostic_interval(
  const logging_options_s & options)
{
  if (options.enable_queue_diagnostics &&
    options.queue_diagnostic_interval <= std::chrono::milliseconds::zero())
  {
    throw std::invalid_argument(
      "Log queue diagnostic interval must be positive");
  }
  return options.queue_diagnostic_interval;
}

std::unique_ptr<abstract_log_sink_c> validate_sink(
  std::unique_ptr<abstract_log_sink_c> sink)
{
  if (sink == nullptr) {
    throw std::invalid_argument("Logger sink cannot be null");
  }
  return sink;
}

std::unique_ptr<abstract_log_sink_c> make_node_log_sink(
  const std::string_view node_name,
  const logging_options_s & options)
{
  const common::string64_t validated_node_name =
    validate_node_name(node_name);
  static_cast<void>(validate_log_level(options.minimum_level));
  static_cast<void>(validate_diagnostic_interval(options));
  if (options.queue_capacity == 0U) {
    throw std::invalid_argument("Log queue capacity must be positive");
  }
  return std::make_unique<node_log_sink_c>(
    validated_node_name.view(),
    options.log_directory.view(),
    options.file_size_bytes,
    options.max_files,
    options.enable_console_log);
}

}  // namespace

local_log_backend_c::local_log_backend_c(
  const std::string_view node_name,
  const logging_options_s & options)
: local_log_backend_c(
    node_name,
    options,
    make_node_log_sink(node_name, options))
{
}

local_log_backend_c::local_log_backend_c(
  const std::string_view node_name,
  const logging_options_s & options,
  std::unique_ptr<abstract_log_sink_c> sink)
: m_node_name(validate_node_name(node_name)),
  m_minimum_level(validate_log_level(options.minimum_level)),
  m_queue_diagnostic_interval(validate_diagnostic_interval(options)),
  m_queue(static_cast<std::size_t>(options.queue_capacity)),
  m_sink(validate_sink(std::move(sink))),
  m_diagnostic_sink(
    options.enable_diagnostic_dds ?
    std::make_unique<diagnostic_dds_sink_c>() :
    nullptr),
  m_processed_record_count(0U),
  m_sink_failure_count(0U),
  m_flush_count(0U),
  m_running(true),
  m_diagnostic_mutex(),
  m_diagnostic_condition(),
  m_diagnostic_thread(
    options.enable_queue_diagnostics ?
    std::jthread(
      [this](const std::stop_token & stop_token) noexcept {
        // The member jthread is joined before this backend is destroyed.
        this->diagnostic_loop(stop_token);
      }) :
    std::jthread()),
  m_writer_thread(&local_log_backend_c::writer_loop, this)
{
}

local_log_backend_c::~local_log_backend_c() noexcept
{
  this->shutdown();
}

bool local_log_backend_c::should_log(const log_level_e level) const noexcept
{
  return level >= m_minimum_level && level <= log_level_e::fatal;
}

void local_log_backend_c::enqueue(
  const log_level_e level,
  const std::string_view message,
  const std::source_location & location) noexcept
{
  log_record_s record;
  record.timestamp_ns = get_realtime_ns();
  record.level = level;
  record.thread_name = get_current_thread_name();
  record.source_file.assign(get_source_basename(location.file_name()));
  record.source_line = static_cast<common::uint32_t>(location.line());
  record.message.assign(message);
  // The queue API uses an rvalue reference to make record transfer explicit.
  static_cast<void>(m_queue.try_push(
      std::move(record)));  // NOLINT(performance-move-const-arg)
}

void local_log_backend_c::shutdown() noexcept
{
  const bool was_running = m_running.exchange(false, std::memory_order_acq_rel);
  if (!was_running) {
    return;
  }

  m_queue.shutdown();
  m_diagnostic_thread.request_stop();
  m_diagnostic_condition.notify_all();
  if (m_diagnostic_thread.joinable()) {
    m_diagnostic_thread.join();
  }

  if (m_writer_thread.joinable()) {
    m_writer_thread.join();
  }
}

void local_log_backend_c::writer_loop() noexcept
{
  configure_writer_thread();
  common::uint64_t last_dropped_record_count = 0U;
  log_record_s record;
  while (m_queue.wait_and_pop(record)) {
    const common::uint64_t dropped_record_count =
      m_queue.get_dropped_record_count();
    if (dropped_record_count > last_dropped_record_count) {
      this->write_drop_notice(
        record,
        dropped_record_count - last_dropped_record_count);
      last_dropped_record_count = dropped_record_count;
    }

    if (!m_sink->write(record)) {
      m_sink_failure_count.fetch_add(1U, std::memory_order_relaxed);
    }
    m_processed_record_count.fetch_add(1U, std::memory_order_relaxed);
    if (m_diagnostic_sink != nullptr) {
      m_diagnostic_sink->add(m_node_name.view(), record);
      m_diagnostic_sink->flush_if_due();
    }
    if (m_queue.consume_pressure_request()) {
      this->flush_sink();
    }
  }

  if (m_diagnostic_sink != nullptr) {
    m_diagnostic_sink->flush();
  }
  this->flush_sink();
}

void local_log_backend_c::flush_sink() noexcept
{
  if (!m_sink->flush()) {
    m_sink_failure_count.fetch_add(1U, std::memory_order_relaxed);
  }
  m_flush_count.fetch_add(1U, std::memory_order_relaxed);
}

void local_log_backend_c::diagnostic_loop(
  const std::stop_token & stop_token) noexcept
{
  configure_diagnostic_thread();
  log_queue_statistics_s previous_statistics = m_queue.get_statistics();
  std::unique_lock<std::mutex> lock(m_diagnostic_mutex);
  while (!stop_token.stop_requested()) {
    static_cast<void>(m_diagnostic_condition.wait_for(
        lock,
        stop_token,
        m_queue_diagnostic_interval,
      []() noexcept {
        return false;
        }));
    if (stop_token.stop_requested()) {
      break;
    }

    lock.unlock();
    previous_statistics =
      this->enqueue_queue_diagnostics(previous_statistics);
    lock.lock();
  }
}

log_queue_statistics_s local_log_backend_c::enqueue_queue_diagnostics(
  const log_queue_statistics_s & previous_statistics) noexcept
{
  const log_queue_statistics_s statistics = m_queue.get_statistics();
  const uint64_t capacity_drop_delta =
    statistics.dropped_capacity_count -
    previous_statistics.dropped_capacity_count;
  const uint64_t contention_drop_delta =
    statistics.dropped_contention_count -
    previous_statistics.dropped_contention_count;
  const uint64_t pressure_event_delta =
    statistics.pressure_event_count -
    previous_statistics.pressure_event_count;
  const uint64_t peak_percent = statistics.capacity > 0U ?
    (statistics.peak_occupancy * 100U) / statistics.capacity :
    0U;
  const char * const buffer_recommendation =
    statistics.dropped_capacity_count > 0U ?
    "increase" :
    (statistics.pressure_event_count > 0U ? "monitor" : "ok");

  std::array<char, common::string256_t::capacity() + 1U> message{};
  const int32_t message_size = static_cast<int32_t>(std::snprintf(
      message.data(),
      message.size(),
      "LogQ buffer=%s cap=%" PRIu64 " used=%" PRIu64
      " peak=%" PRIu64 "%% accepted=%" PRIu64
      " processed=%" PRIu64 " full_delta=%" PRIu64
      " busy_delta=%" PRIu64 " pressure_delta=%" PRIu64
      " flushes=%" PRIu64 " sink_fail=%" PRIu64,
      buffer_recommendation,
      statistics.capacity,
      statistics.occupancy,
      peak_percent,
      statistics.accepted_record_count,
      m_processed_record_count.load(std::memory_order_relaxed),
      capacity_drop_delta,
      contention_drop_delta,
      pressure_event_delta,
      m_flush_count.load(std::memory_order_relaxed),
      m_sink_failure_count.load(std::memory_order_relaxed)));
  if (message_size <= 0) {
    return statistics;
  }

  log_record_s record;
  record.timestamp_ns = get_realtime_ns();
  record.level =
    capacity_drop_delta > 0U ||
    contention_drop_delta > 0U ||
    m_sink_failure_count.load(std::memory_order_relaxed) > 0U ?
    log_level_e::warning :
    log_level_e::info;
  record.thread_name = "log_diag";
  record.source_file = "log_backend";
  record.message.assign(message.data());
  // The queue API uses an rvalue reference to make record transfer explicit.
  static_cast<void>(m_queue.try_push(
      std::move(record)));  // NOLINT(performance-move-const-arg)
  return statistics;
}

void local_log_backend_c::write_drop_notice(
  const log_record_s & next_record,
  const common::uint64_t dropped_record_count) noexcept
{
  std::array<char, common::string256_t::capacity() + 1U> message{};
  const int32_t message_size = static_cast<int32_t>(std::snprintf(
      message.data(),
      message.size(),
      "Logger dropped %" PRIu64 " record(s)",
      dropped_record_count));
  if (message_size <= 0) {
    return;
  }

  log_record_s drop_notice;
  drop_notice.timestamp_ns = next_record.timestamp_ns;
  drop_notice.level = log_level_e::warning;
  drop_notice.thread_name = "log_writer";
  drop_notice.source_file = "log_backend";
  drop_notice.message.assign(message.data());
  if (!m_sink->write(drop_notice)) {
    m_sink_failure_count.fetch_add(1U, std::memory_order_relaxed);
  }
  if (m_diagnostic_sink != nullptr) {
    m_diagnostic_sink->add(m_node_name.view(), drop_notice);
  }
}

}  // namespace logging
