#pragma once

#include <vector>
#include <cstdint>
#include <optional>
#include <condition_variable>
#include <atomic>
#include <chrono>

#include "base_node/mutex.hpp"
#include "base_node/ring_buffer.hpp"

namespace base_node
{
namespace topic
{

template<typename T>
class concurrent_ring_buffer_c : public ring_buffer_c<T>
{
public:
  using size_type = typename ring_buffer_c<T>::size_type;

  explicit concurrent_ring_buffer_c(size_type const max_size)
  : ring_buffer_c<T>(max_size)
  {
  }

  concurrent_ring_buffer_c() : concurrent_ring_buffer_c(0) {}

  ~concurrent_ring_buffer_c() override = default;

  void clear() noexcept override
  {
    std::lock_guard<base_node::sync::mutex_c> lock(m_mutex);
    ring_buffer_c<T>::clear();
  }

  bool empty() const noexcept override
  {
    std::lock_guard<base_node::sync::mutex_c> lock(m_mutex);
    return ring_buffer_c<T>::empty();
  }

  size_type size() const noexcept override
  {
    std::lock_guard<base_node::sync::mutex_c> lock(m_mutex);
    return ring_buffer_c<T>::size();
  }

  size_type capacity() const noexcept override
  {
    return ring_buffer_c<T>::capacity();
  }

  bool push_back(const T& value) override
  {
    bool pushed = false;
    {
      std::lock_guard<base_node::sync::mutex_c> lock(m_mutex);
      pushed = ring_buffer_c<T>::push_back(value);
    }
    if (pushed) m_cv.notify_one();
    return pushed;
  }

  std::optional<T> pop_front() override
  {
    std::lock_guard<base_node::sync::mutex_c> lock(m_mutex);
    return ring_buffer_c<T>::pop_front();
  }

  std::optional<T> wait_and_pop_front(const std::atomic<bool>& running_flag)
  {
    m_mutex.lock();
    while (ring_buffer_c<T>::empty() && running_flag.load()) {
      m_cv.wait(m_mutex);
    }
    
    if (ring_buffer_c<T>::empty()) {
      m_mutex.unlock();
      return std::nullopt;
    }
    
    std::optional<T> value = ring_buffer_c<T>::pop_front();
    m_mutex.unlock();
    return value;
  }

  template<class Rep, class Period>
  std::optional<T> wait_and_pop_front_timeout(
    const std::chrono::duration<Rep, Period>& timeout,
    const std::atomic<bool>& running_flag)
  {
    m_mutex.lock();
    if (ring_buffer_c<T>::empty() && running_flag.load()) {
      m_cv.wait_for(m_mutex, timeout, [this, &running_flag]() {
        return !ring_buffer_c<T>::empty() || !running_flag.load();
      });
    }
    
    if (ring_buffer_c<T>::empty()) {
      m_mutex.unlock();
      return std::nullopt;
    }
    
    std::optional<T> value = ring_buffer_c<T>::pop_front();
    m_mutex.unlock();
    return value;
  }

  void shutdown()
  {
    m_cv.notify_all();
  }

private:
  mutable base_node::sync::mutex_c m_mutex;
  std::condition_variable_any m_cv;
};

}  // namespace topic
}  // namespace base_node
