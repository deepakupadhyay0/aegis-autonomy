#pragma once

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace base_core
{

/// @brief Fixed-capacity O(1) ring buffer that overwrites the oldest item when full.
/// Not thread-safe (use concurrent_ring_buffer_c for callbacks).
template<typename T>
class ring_buffer_c
{
public:
  using size_type = std::size_t;

  explicit ring_buffer_c(size_type const max_size)
  : m_max_size(max_size + 1U),
    m_buffer(m_max_size),
    m_head(0),
    m_tail(0)
  {
  }

  ring_buffer_c()
  : ring_buffer_c(0U) {}

  void clear()
  {
    while (this->discard_front()) {
    }
  }

  bool empty() const noexcept
  {
    return m_head == m_tail;
  }

  size_type size() const noexcept
  {
    return (m_tail + m_max_size - m_head) % m_max_size;
  }

  size_type capacity() const noexcept
  {
    return m_max_size - 1;
  }

  bool push_back(const T & value)
  {
    return this->push_back_impl(value);
  }

  bool push_back(T && value)
  {
    return this->push_back_impl(std::move(value));
  }

  std::optional<T> pop_front()
  {
    if (m_head == m_tail) {
      return std::nullopt;
    }

    T value = std::move(m_buffer[m_head]);
    m_buffer[m_head] = T{};
    m_head = (m_head + 1U) % m_max_size;
    return value;
  }

private:
  bool discard_front()
  {
    if (m_head == m_tail) {
      return false;
    }

    m_buffer[m_head] = T{};
    m_head = (m_head + 1U) % m_max_size;
    return true;
  }

  template<typename value_t>
  bool push_back_impl(value_t && value)
  {
    if (this->capacity() == 0U) {
      return false;
    }

    const size_type next_tail = (m_tail + 1) % m_max_size;
    if (next_tail == m_head) {
      this->discard_front();
    }
    m_buffer[m_tail] = std::forward<value_t>(value);
    m_tail = next_tail;
    return true;
  }
  size_type m_max_size;
  std::vector<T> m_buffer;
  size_type m_head;
  size_type m_tail;
};

}  // namespace base_core
