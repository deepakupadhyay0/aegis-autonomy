#pragma once

#include "common/numeric_types.hpp"

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace common
{

/// Fixed-capacity, single-consumer queue. Producers never wait: contention,
/// full capacity, or shutdown rejects the incoming value.
template<typename value_t>
class bounded_mpsc_queue_c final
{
  static_assert(
    std::is_nothrow_default_constructible_v<value_t>,
    "Queue values must be nothrow default constructible");
  static_assert(
    std::is_nothrow_move_assignable_v<value_t>,
    "Queue values must be nothrow move assignable");

public:
  explicit bounded_mpsc_queue_c(const std::size_t capacity)
  : m_storage(capacity),
    m_head(0U),
    m_tail(0U),
    m_size(0U),
    m_generation(0U),
    m_dropped_count(0U),
    m_running(true)
  {
    if (capacity == 0U) {
      throw std::invalid_argument("Queue capacity must be positive");
    }
  }

  ~bounded_mpsc_queue_c() noexcept
  {
    this->shutdown();
  }

  bounded_mpsc_queue_c(const bounded_mpsc_queue_c &) = delete;
  bounded_mpsc_queue_c & operator=(const bounded_mpsc_queue_c &) = delete;
  bounded_mpsc_queue_c(bounded_mpsc_queue_c &&) = delete;
  bounded_mpsc_queue_c & operator=(bounded_mpsc_queue_c &&) = delete;

  bool try_push(value_t && value) noexcept
  {
    if (!m_running.load(std::memory_order_acquire) ||
      m_lock.test_and_set(std::memory_order_acquire))
    {
      m_dropped_count.fetch_add(1U, std::memory_order_relaxed);
      return false;
    }
    if (!m_running.load(std::memory_order_relaxed) ||
      m_size == m_storage.size())
    {
      this->unlock();
      m_dropped_count.fetch_add(1U, std::memory_order_relaxed);
      return false;
    }

    m_storage[m_tail] = std::move(value);
    m_tail = this->next_index(m_tail);
    ++m_size;
    this->unlock();
    m_generation.fetch_add(1U, std::memory_order_release);
    m_generation.notify_one();
    return true;
  }

  bool try_pop(value_t & value) noexcept
  {
    while (m_lock.test_and_set(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    if (m_size == 0U) {
      this->unlock();
      return false;
    }
    value = std::move(m_storage[m_head]);
    m_storage[m_head] = value_t{};
    m_head = this->next_index(m_head);
    --m_size;
    this->unlock();
    return true;
  }

  bool wait_and_pop(value_t & value) noexcept
  {
    while (true) {
      if (this->try_pop(value)) {
        return true;
      }
      if (!m_running.load(std::memory_order_acquire)) {
        return false;
      }
      const uint64_t generation =
        m_generation.load(std::memory_order_acquire);
      if (this->try_pop(value)) {
        return true;
      }
      if (!m_running.load(std::memory_order_acquire)) {
        return false;
      }
      m_generation.wait(generation, std::memory_order_acquire);
    }
  }

  uint64_t get_dropped_count() const noexcept
  {
    return m_dropped_count.load(std::memory_order_relaxed);
  }

  void shutdown() noexcept
  {
    while (m_lock.test_and_set(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    if (!m_running.exchange(false, std::memory_order_acq_rel)) {
      this->unlock();
      return;
    }
    this->unlock();
    m_generation.fetch_add(1U, std::memory_order_release);
    m_generation.notify_all();
  }

private:
  std::size_t next_index(const std::size_t index) const noexcept
  {
    const std::size_t next = index + 1U;
    return next == m_storage.size() ? 0U : next;
  }

  void unlock() noexcept
  {
    m_lock.clear(std::memory_order_release);
  }

  std::vector<value_t> m_storage;
  std::size_t m_head;
  std::size_t m_tail;
  std::size_t m_size;
  std::atomic_flag m_lock = ATOMIC_FLAG_INIT;
  /// The queue flag publishes slot/index changes with release/acquire.
  /// Generation release/acquire prevents missed producer and shutdown wakes.
  /// The dropped counter is diagnostic only and therefore uses relaxed order.
  std::atomic<uint64_t> m_generation;
  std::atomic<uint64_t> m_dropped_count;
  std::atomic<bool> m_running;
};

}  // namespace common
