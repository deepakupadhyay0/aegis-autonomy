#pragma once

#include <vector>
#include <cstdint>
#include <optional>
#include <condition_variable>
#include <atomic>

#include "base_node/mutex.hpp"

namespace base_node
{
namespace topic
{

template<typename T>
class concurrent_ring_buffer_c
{
public:
  using size_type = std::size_t;

  explicit concurrent_ring_buffer_c(size_type const max_size)
  : m_max_size(max_size > 0 ? max_size + 1 : 1),
    m_buffer(m_max_size),
    m_head(0),
    m_tail(0)
  {
  }

  concurrent_ring_buffer_c() : concurrent_ring_buffer_c(0) {}

  void clear() noexcept
  {
    std::lock_guard<base_node::sync::mutex_c> lock(m_mutex);
    m_head = 0;
    m_tail = 0;
  }

  bool empty() const noexcept
  {
    std::lock_guard<base_node::sync::mutex_c> lock(m_mutex);
    return m_head == m_tail;
  }

  size_type capacity() const noexcept
  {
    return m_max_size - 1;
  }

  bool push_back(const T& value)
  {
    bool pushed = false;
    {
      std::lock_guard<base_node::sync::mutex_c> lock(m_mutex);
      const size_type next_tail = (m_tail + 1) % m_max_size;
      
      if (next_tail != m_head) {
        m_buffer[m_tail] = value;
        m_tail = next_tail;
        pushed = true;
      }
    }
    if (pushed) m_cv.notify_one();
    return pushed;
  }

  std::optional<T> pop_front()
  {
    std::lock_guard<base_node::sync::mutex_c> lock(m_mutex);
    
    if (m_head == m_tail) {
      return std::nullopt; 
    }
    
    T value = m_buffer[m_head];
    m_head = (m_head + 1) % m_max_size;
    return value;
  }

  std::optional<T> wait_and_pop_front(const std::atomic<bool>& running_flag)
  {
    m_mutex.lock();
    while (m_head == m_tail && running_flag.load()) {
      m_cv.wait(m_mutex);
    }
    
    if (m_head == m_tail) {
      m_mutex.unlock();
      return std::nullopt;
    }
    
    T value = m_buffer[m_head];
    m_head = (m_head + 1) % m_max_size;
    m_mutex.unlock();
    return value;
  }

  void shutdown()
  {
    m_cv.notify_all();
  }

private:
  size_type m_max_size;
  std::vector<T> m_buffer;
  size_type m_head;
  size_type m_tail;
  mutable base_node::sync::mutex_c m_mutex;
  std::condition_variable_any m_cv;
};

}  // namespace topic
}  // namespace base_node
