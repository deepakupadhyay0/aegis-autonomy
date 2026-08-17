#pragma once

#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace base_core
{

/// @brief Fixed-capacity FIFO queue that rejects new values when full.
///
/// Storage is allocated during construction. The queue is not thread-safe;
/// callers that share it between threads must provide synchronization.
template<typename value_t>
class bounded_queue_c final
{
  static_assert(
    std::is_nothrow_default_constructible_v<value_t>,
    "Queue values must be nothrow default constructible");
  static_assert(
    std::is_nothrow_move_assignable_v<value_t>,
    "Queue values must be nothrow move assignable");

public:
  using size_type = std::size_t;

  explicit bounded_queue_c(const size_type capacity)
  : m_storage(validate_capacity(capacity)),
    m_head(0U),
    m_tail(0U),
    m_size(0U)
  {
  }

  ~bounded_queue_c() noexcept = default;

  bounded_queue_c(const bounded_queue_c &) = delete;
  bounded_queue_c & operator=(const bounded_queue_c &) = delete;
  bounded_queue_c(bounded_queue_c &&) = delete;
  bounded_queue_c & operator=(bounded_queue_c &&) = delete;

  bool try_push(const value_t & value)
    noexcept(std::is_nothrow_copy_assignable_v<value_t>)
  {
    return this->try_push_impl(value);
  }

  bool try_push(value_t && value) noexcept
  {
    return this->try_push_impl(std::move(value));
  }

  bool try_pop(value_t & value) noexcept
  {
    if (this->empty()) {
      return false;
    }

    value = std::move(m_storage[m_head]);
    m_storage[m_head] = value_t{};
    m_head = this->next_index(m_head);
    --m_size;
    return true;
  }

  void clear() noexcept
  {
    while (!this->empty()) {
      m_storage[m_head] = value_t{};
      m_head = this->next_index(m_head);
      --m_size;
    }
    m_tail = m_head;
  }

  bool empty() const noexcept
  {
    return m_size == 0U;
  }

  size_type size() const noexcept
  {
    return m_size;
  }

  size_type capacity() const noexcept
  {
    return m_storage.size();
  }

private:
  static size_type validate_capacity(const size_type capacity)
  {
    if (capacity == 0U) {
      throw std::invalid_argument("Queue capacity must be positive");
    }
    return capacity;
  }

  template<typename source_t>
  bool try_push_impl(source_t && value)
    noexcept(std::is_nothrow_assignable_v<value_t &, source_t &&>)
  {
    if (m_size == m_storage.size()) {
      return false;
    }

    m_storage[m_tail] = std::forward<source_t>(value);
    m_tail = this->next_index(m_tail);
    ++m_size;
    return true;
  }

  size_type next_index(const size_type index) const noexcept
  {
    const size_type next = index + 1U;
    return next == m_storage.size() ? 0U : next;
  }

  std::vector<value_t> m_storage;
  size_type m_head;
  size_type m_tail;
  size_type m_size;
};

}  // namespace base_core
