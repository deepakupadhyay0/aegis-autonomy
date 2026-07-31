#include "logging/log_queue.hpp"

#include <stdexcept>
#include <thread>
#include <utility>

namespace logging
{

log_queue_c::log_queue_c(const std::size_t capacity)
: m_storage(capacity),
  m_head(0U),
  m_tail(0U),
  m_size(0U),
  m_high_watermark(capacity - (capacity / 10U)),
  m_low_watermark(capacity / 2U),
  m_pressure_active(false),
  m_generation(0U),
  m_occupancy(0U),
  m_peak_occupancy(0U),
  m_accepted_record_count(0U),
  m_dropped_capacity_count(0U),
  m_dropped_contention_count(0U),
  m_dropped_stopped_count(0U),
  m_pressure_event_count(0U),
  m_pressure_requested(false),
  m_running(true)
{
  if (capacity == 0U) {
    throw std::invalid_argument("Log queue capacity must be positive");
  }
}

log_queue_c::~log_queue_c() noexcept
{
  this->shutdown();
}

bool log_queue_c::try_push(
  log_record_s && record) noexcept
{
  if (!m_running.load(std::memory_order_acquire)) {
    m_dropped_stopped_count.fetch_add(1U, std::memory_order_relaxed);
    return false;
  }
  if (m_queue_lock.test_and_set(std::memory_order_acquire)) {
    m_dropped_contention_count.fetch_add(1U, std::memory_order_relaxed);
    return false;
  }

  if (!m_running.load(std::memory_order_relaxed)) {
    this->unlock_queue();
    m_dropped_stopped_count.fetch_add(1U, std::memory_order_relaxed);
    return false;
  }

  const std::size_t capacity = m_storage.size();
  if (m_size == capacity) {
    this->unlock_queue();
    m_dropped_capacity_count.fetch_add(1U, std::memory_order_relaxed);
    return false;
  }

  m_storage[m_tail] = std::move(record);
  m_tail = (m_tail + 1U) % capacity;
  ++m_size;
  const uint64_t occupancy = static_cast<uint64_t>(m_size);
  m_occupancy.store(occupancy, std::memory_order_release);
  if (m_peak_occupancy.load(std::memory_order_relaxed) < occupancy) {
    // The queue flag serializes all producers, so peak has a single writer.
    m_peak_occupancy.store(occupancy, std::memory_order_relaxed);
  }
  if (!m_pressure_active && m_size >= m_high_watermark) {
    m_pressure_active = true;
    m_pressure_requested.store(true, std::memory_order_release);
    m_pressure_event_count.fetch_add(1U, std::memory_order_relaxed);
  }

  this->unlock_queue();
  m_accepted_record_count.fetch_add(1U, std::memory_order_relaxed);
  m_generation.fetch_add(1U, std::memory_order_release);
  m_generation.notify_one();
  return true;
}

bool log_queue_c::try_pop(
  log_record_s & record) noexcept
{
  while (m_queue_lock.test_and_set(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  if (m_size == 0U) {
    this->unlock_queue();
    return false;
  }

  record = std::move(m_storage[m_head]);
  m_storage[m_head] = log_record_s{};
  m_head = (m_head + 1U) % m_storage.size();
  --m_size;
  m_occupancy.store(
    static_cast<uint64_t>(m_size),
    std::memory_order_release);
  if (m_pressure_active && m_size <= m_low_watermark) {
    m_pressure_active = false;
  }
  this->unlock_queue();
  return true;
}

bool log_queue_c::wait_and_pop(
  log_record_s & record) noexcept
{
  while (true) {
    if (this->try_pop(record)) {
      return true;
    }
    if (!m_running.load(std::memory_order_acquire)) {
      return false;
    }

    const uint64_t observed_generation =
      m_generation.load(std::memory_order_acquire);
    if (this->try_pop(record)) {
      return true;
    }
    if (!m_running.load(std::memory_order_acquire)) {
      return false;
    }
    m_generation.wait(observed_generation, std::memory_order_acquire);
  }
}

uint64_t log_queue_c::get_dropped_record_count() const noexcept
{
  return
    m_dropped_capacity_count.load(std::memory_order_relaxed) +
    m_dropped_contention_count.load(std::memory_order_relaxed) +
    m_dropped_stopped_count.load(std::memory_order_relaxed);
}

log_queue_statistics_s log_queue_c::get_statistics() const noexcept
{
  log_queue_statistics_s statistics;
  statistics.capacity = static_cast<uint64_t>(m_storage.size());
  statistics.occupancy = m_occupancy.load(std::memory_order_acquire);
  statistics.peak_occupancy =
    m_peak_occupancy.load(std::memory_order_relaxed);
  statistics.accepted_record_count =
    m_accepted_record_count.load(std::memory_order_relaxed);
  statistics.dropped_capacity_count =
    m_dropped_capacity_count.load(std::memory_order_relaxed);
  statistics.dropped_contention_count =
    m_dropped_contention_count.load(std::memory_order_relaxed);
  statistics.dropped_stopped_count =
    m_dropped_stopped_count.load(std::memory_order_relaxed);
  statistics.pressure_event_count =
    m_pressure_event_count.load(std::memory_order_relaxed);
  return statistics;
}

bool log_queue_c::consume_pressure_request() noexcept
{
  return m_pressure_requested.exchange(false, std::memory_order_acq_rel);
}

void log_queue_c::shutdown() noexcept
{
  const bool was_running = m_running.exchange(false, std::memory_order_acq_rel);
  if (!was_running) {
    return;
  }
  m_generation.fetch_add(1U, std::memory_order_release);
  m_generation.notify_all();
}

void log_queue_c::unlock_queue() noexcept
{
  m_queue_lock.clear(std::memory_order_release);
}

}  // namespace logging
