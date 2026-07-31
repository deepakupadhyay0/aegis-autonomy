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
  m_generation(0U),
  m_dropped_record_count(0U),
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
  common::logging::log_record_s && record) noexcept
{
  if (!m_running.load(std::memory_order_acquire)) {
    m_dropped_record_count.fetch_add(1U, std::memory_order_relaxed);
    return false;
  }
  if (m_queue_lock.test_and_set(std::memory_order_acquire)) {
    m_dropped_record_count.fetch_add(1U, std::memory_order_relaxed);
    return false;
  }

  if (!m_running.load(std::memory_order_relaxed)) {
    this->unlock_queue();
    m_dropped_record_count.fetch_add(1U, std::memory_order_relaxed);
    return false;
  }

  const std::size_t capacity = m_storage.size();
  if (m_size == capacity) {
    m_storage[m_tail] = std::move(record);
    m_tail = (m_tail + 1U) % capacity;
    m_head = m_tail;
    m_dropped_record_count.fetch_add(1U, std::memory_order_relaxed);
  } else {
    m_storage[m_tail] = std::move(record);
    m_tail = (m_tail + 1U) % capacity;
    ++m_size;
  }

  this->unlock_queue();
  m_generation.fetch_add(1U, std::memory_order_release);
  m_generation.notify_one();
  return true;
}

bool log_queue_c::try_pop(
  common::logging::log_record_s & record) noexcept
{
  while (m_queue_lock.test_and_set(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  if (m_size == 0U) {
    this->unlock_queue();
    return false;
  }

  record = std::move(m_storage[m_head]);
  m_storage[m_head] = common::logging::log_record_s{};
  m_head = (m_head + 1U) % m_storage.size();
  --m_size;
  this->unlock_queue();
  return true;
}

bool log_queue_c::wait_and_pop(
  common::logging::log_record_s & record) noexcept
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
  return m_dropped_record_count.load(std::memory_order_relaxed);
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
