#pragma once

#include <vector>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include "base_node/buffer_base.hpp"

namespace base_node
{
namespace topic
{

/// @brief A highly optimized O(1) single-threaded ring buffer.
/// Not thread-safe (use concurrent_ring_buffer_c for callbacks).
template<typename T>
class ring_buffer_c : public buffer_base_c<T>
{
public:
  using size_type = typename buffer_base_c<T>::size_type;

  explicit ring_buffer_c(size_type const max_size)
  : m_max_size(max_size > 0 ? max_size + 1 : 1),
    m_buffer(m_max_size),
    m_head(0),
    m_tail(0)
  {
  }

  ring_buffer_c() : ring_buffer_c(0) {}

  ~ring_buffer_c() override = default;

  void clear() noexcept override
  {
    m_head = 0;
    m_tail = 0;
  }

  bool empty() const noexcept override
  {
    return m_head == m_tail;
  }

  size_type size() const noexcept override
  {
    return (m_tail + m_max_size - m_head) % m_max_size;
  }

  size_type capacity() const noexcept override
  {
    return m_max_size - 1;
  }

  bool push_back(const T& value) override
  {
    const size_type next_tail = (m_tail + 1) % m_max_size;
    if (next_tail == m_head) {
      return false; 
    }
    m_buffer[m_tail] = value;
    m_tail = next_tail;
    return true;
  }

  std::optional<T> pop_front() override
  {
    if (m_head == m_tail) {
      return std::nullopt; 
    }
    T value = m_buffer[m_head];
    m_head = (m_head + 1) % m_max_size;
    return value;
  }

private:
  size_type m_max_size;
  std::vector<T> m_buffer;
  size_type m_head;
  size_type m_tail;
};

}  // namespace topic
}  // namespace base_node
