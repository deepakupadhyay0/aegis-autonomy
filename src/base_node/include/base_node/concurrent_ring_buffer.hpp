#pragma once

#include "base_node/mutex.hpp"
#include "base_node/ring_buffer.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <utility>

namespace base_node
{
namespace topic
{

/// @brief Fixed-capacity thread-safe queue with explicit shutdown behavior.
template<typename value_t>
class concurrent_ring_buffer_c
{
public:
  using size_type = typename ring_buffer_c<value_t>::size_type;

  explicit concurrent_ring_buffer_c(
    const size_type max_size,
    const base_node::sync::priority_inheritance_e priority_inheritance =
    base_node::sync::priority_inheritance_e::enabled)
  : m_buffer(max_size),
    m_mutex(priority_inheritance),
    m_condition(),
    m_running(true)
  {
  }

  concurrent_ring_buffer_c()
  : concurrent_ring_buffer_c(0U)
  {
  }

  ~concurrent_ring_buffer_c() noexcept
  {
    this->shutdown();
  }

  concurrent_ring_buffer_c(const concurrent_ring_buffer_c &) = delete;
  concurrent_ring_buffer_c & operator=(const concurrent_ring_buffer_c &) = delete;
  concurrent_ring_buffer_c(concurrent_ring_buffer_c &&) = delete;
  concurrent_ring_buffer_c & operator=(concurrent_ring_buffer_c &&) = delete;

  void clear()
  {
    std::lock_guard<base_node::sync::mutex_c> lock(m_mutex);
    m_buffer.clear();
  }

  bool8_t empty() const
  {
    std::lock_guard<base_node::sync::mutex_c> lock(m_mutex);
    return m_buffer.empty();
  }

  size_type size() const
  {
    std::lock_guard<base_node::sync::mutex_c> lock(m_mutex);
    return m_buffer.size();
  }

  size_type capacity() const noexcept
  {
    return m_buffer.capacity();
  }

  bool8_t push_back(const value_t & value)
  {
    return this->push_back_impl(value);
  }

  bool8_t push_back(value_t && value)
  {
    return this->push_back_impl(std::move(value));
  }

  std::optional<value_t> pop_front()
  {
    std::lock_guard<base_node::sync::mutex_c> lock(m_mutex);
    return m_buffer.pop_front();
  }

  std::optional<value_t> wait_and_pop_front()
  {
    std::unique_lock<base_node::sync::mutex_c> lock(m_mutex);
    m_condition.wait(lock, [this]() {
      return !m_buffer.empty() || !m_running.load();
    });

    if (!m_running.load()) {
      return std::nullopt;
    }
    return m_buffer.pop_front();
  }

  template<class rep_t, class period_t>
  std::optional<value_t> wait_and_pop_front_timeout(
    const std::chrono::duration<rep_t, period_t> & timeout)
  {
    std::unique_lock<base_node::sync::mutex_c> lock(m_mutex);
    const bool8_t awakened = m_condition.wait_for(lock, timeout, [this]() {
      return !m_buffer.empty() || !m_running.load();
    });

    if (!awakened || !m_running.load()) {
      return std::nullopt;
    }
    return m_buffer.pop_front();
  }

  void shutdown() noexcept
  {
    m_running.store(false);
    m_condition.notify_all();
  }

private:
  template<typename source_t>
  bool8_t push_back_impl(source_t && value)
  {
    bool8_t inserted = false;
    {
      std::lock_guard<base_node::sync::mutex_c> lock(m_mutex);
      if (m_running.load()) {
        inserted = m_buffer.push_back(std::forward<source_t>(value));
      }
    }
    if (inserted) {
      m_condition.notify_one();
    }
    return inserted;
  }

  ring_buffer_c<value_t> m_buffer;
  mutable base_node::sync::mutex_c m_mutex;
  std::condition_variable_any m_condition;
  std::atomic<bool8_t> m_running;
};

}  // namespace topic
}  // namespace base_node
