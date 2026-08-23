#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory_resource>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace common
{

/// @brief Runtime-capacity vector that allocates its element storage once.
/// The supplied resource is borrowed and must outlive this vector. Allocations
/// performed internally by value_t are covered only when value_t also uses the
/// same bounded resource.
template<typename value_t>
class strict_vector_c final
{
private:
  class memory_resource_c final : public std::pmr::memory_resource
  {
public:
    explicit memory_resource_c(const std::size_t max_bytes)
    : m_upstream(std::pmr::get_default_resource()),
      m_max_bytes(max_bytes),
      m_current_bytes(0U),
      m_peak_bytes(0U),
      m_failed_allocations(0U)
    {
    }

    ~memory_resource_c() noexcept override = default;

    memory_resource_c(const memory_resource_c &) = delete;
    memory_resource_c & operator=(const memory_resource_c &) = delete;
    memory_resource_c(memory_resource_c &&) = delete;
    memory_resource_c & operator=(memory_resource_c &&) = delete;

    std::size_t max_bytes() const noexcept
    {
      return m_max_bytes;
    }

    std::size_t current_bytes() const noexcept
    {
      return m_current_bytes;
    }

    std::size_t peak_bytes() const noexcept
    {
      return m_peak_bytes;
    }

    std::size_t failed_allocations() const noexcept
    {
      return m_failed_allocations;
    }

private:
    void * do_allocate(
      const std::size_t bytes,
      const std::size_t alignment) override
    {
      if (bytes > m_max_bytes - m_current_bytes) {
        ++m_failed_allocations;
        throw std::bad_alloc{};
      }

      void * const allocation = m_upstream->allocate(bytes, alignment);
      m_current_bytes += bytes;
      m_peak_bytes = std::max(m_peak_bytes, m_current_bytes);
      return allocation;
    }

    void do_deallocate(
      void * const allocation,
      const std::size_t bytes,
      const std::size_t alignment) override
    {
      m_upstream->deallocate(allocation, bytes, alignment);
      m_current_bytes -= bytes;
    }

    bool do_is_equal(
      const std::pmr::memory_resource & other) const noexcept override
    {
      return this == &other;
    }

    std::pmr::memory_resource * const m_upstream;
    const std::size_t m_max_bytes;
    std::size_t m_current_bytes;
    std::size_t m_peak_bytes;
    std::size_t m_failed_allocations;
  };

public:
  using value_type = value_t;
  using size_type = std::size_t;
  using difference_type = typename std::pmr::vector<value_t>::difference_type;
  using reference = value_t &;
  using const_reference = const value_t &;
  using pointer = value_t *;
  using const_pointer = const value_t *;
  using iterator = typename std::pmr::vector<value_t>::iterator;
  using const_iterator = typename std::pmr::vector<value_t>::const_iterator;
  using reverse_iterator =
    typename std::pmr::vector<value_t>::reverse_iterator;
  using const_reverse_iterator =
    typename std::pmr::vector<value_t>::const_reverse_iterator;

  explicit strict_vector_c(const size_type max_elements)
  : m_owned_resource(
      std::in_place,
      calculate_max_bytes(max_elements)),
    m_values(&m_owned_resource.value()),
    m_max_elements(max_elements)
  {
    m_values.reserve(m_max_elements);
  }

  strict_vector_c(
    const size_type max_elements,
    std::pmr::memory_resource & resource)
  : m_owned_resource(std::nullopt),
    m_values(&resource),
    m_max_elements(validate_max_elements(max_elements))
  {
    m_values.reserve(m_max_elements);
  }

  ~strict_vector_c() noexcept = default;

  strict_vector_c(const strict_vector_c &) = delete;
  strict_vector_c & operator=(const strict_vector_c &) = delete;
  strict_vector_c(strict_vector_c &&) = delete;
  strict_vector_c & operator=(strict_vector_c &&) = delete;

  bool try_push_back(const value_t & value)
  {
    if (this->full()) {
      return false;
    }
    m_values.push_back(value);
    return true;
  }

  bool try_push_back(value_t && value)
  noexcept(std::is_nothrow_move_constructible_v<value_t>)
  {
    if (this->full()) {
      return false;
    }
    m_values.push_back(std::move(value));
    return true;
  }

  template<typename ... argument_t>
  bool try_emplace_back(argument_t &&... arguments)
  noexcept(std::is_nothrow_constructible_v<value_t, argument_t && ...>)
  {
    if (this->full()) {
      return false;
    }
    m_values.emplace_back(std::forward<argument_t>(arguments)...);
    return true;
  }

  bool try_resize(const size_type requested_size)
  {
    if (requested_size > m_max_elements) {
      return false;
    }
    m_values.resize(requested_size);
    return true;
  }

  bool try_resize(
    const size_type requested_size,
    const value_t & value)
  {
    if (requested_size > m_max_elements) {
      return false;
    }
    m_values.resize(requested_size, value);
    return true;
  }

  bool try_assign(const std::span<const value_t> values)
  {
    if (values.size() > m_max_elements) {
      return false;
    }
    m_values.assign(values.begin(), values.end());
    return true;
  }

  bool try_insert(const_iterator position, const value_t & value)
  {
    if (this->full()) {
      return false;
    }
    m_values.insert(position, value);
    return true;
  }

  bool try_insert(const_iterator position, value_t && value)
  {
    if (this->full()) {
      return false;
    }
    m_values.insert(position, std::move(value));
    return true;
  }

  bool try_insert(
    const_iterator position,
    const std::span<const value_t> values)
  {
    if (values.size() > m_max_elements - this->size()) {
      return false;
    }
    m_values.insert(position, values.begin(), values.end());
    return true;
  }

  bool try_pop_back(value_t & value)
  noexcept(std::is_nothrow_move_assignable_v<value_t>&&
  std::is_nothrow_destructible_v<value_t>)
  {
    if (this->empty()) {
      return false;
    }
    value = std::move(m_values.back());
    m_values.pop_back();
    return true;
  }

  value_t * try_at(const size_type index) noexcept
  {
    return index < this->size() ? &m_values[index] : nullptr;
  }

  const value_t * try_at(const size_type index) const noexcept
  {
    return index < this->size() ? &m_values[index] : nullptr;
  }

  reference at(const size_type index)
  {
    return m_values.at(index);
  }

  const_reference at(const size_type index) const
  {
    return m_values.at(index);
  }

  reference operator[](const size_type index) noexcept
  {
    return m_values[index];
  }

  const_reference operator[](const size_type index) const noexcept
  {
    return m_values[index];
  }

  reference front() noexcept
  {
    return m_values.front();
  }

  const_reference front() const noexcept
  {
    return m_values.front();
  }

  reference back() noexcept
  {
    return m_values.back();
  }

  const_reference back() const noexcept
  {
    return m_values.back();
  }

  pointer data() noexcept
  {
    return m_values.data();
  }

  const_pointer data() const noexcept
  {
    return m_values.data();
  }

  std::span<value_t> values() noexcept
  {
    return std::span<value_t>(m_values.data(), m_values.size());
  }

  std::span<const value_t> values() const noexcept
  {
    return std::span<const value_t>(m_values.data(), m_values.size());
  }

  void clear() noexcept(std::is_nothrow_destructible_v<value_t>)
  {
    m_values.clear();
  }

  iterator erase(const_iterator position)
  {
    return m_values.erase(position);
  }

  iterator erase(const_iterator first, const_iterator last)
  {
    return m_values.erase(first, last);
  }

  void pop_back() noexcept(std::is_nothrow_destructible_v<value_t>)
  {
    m_values.pop_back();
  }

  bool empty() const noexcept
  {
    return m_values.empty();
  }

  bool full() const noexcept
  {
    return m_values.size() == m_max_elements;
  }

  size_type size() const noexcept
  {
    return m_values.size();
  }

  size_type capacity() const noexcept
  {
    return m_max_elements;
  }

  size_type max_size() const noexcept
  {
    return m_max_elements;
  }

  bool owns_memory_resource() const noexcept
  {
    return m_owned_resource.has_value();
  }

  std::optional<size_type> memory_limit_bytes() const noexcept
  {
    if (!m_owned_resource.has_value()) {
      return std::nullopt;
    }
    return m_owned_resource->max_bytes();
  }

  std::optional<size_type> current_memory_bytes() const noexcept
  {
    if (!m_owned_resource.has_value()) {
      return std::nullopt;
    }
    return m_owned_resource->current_bytes();
  }

  std::optional<size_type> peak_memory_bytes() const noexcept
  {
    if (!m_owned_resource.has_value()) {
      return std::nullopt;
    }
    return m_owned_resource->peak_bytes();
  }

  std::optional<size_type> failed_allocations() const noexcept
  {
    if (!m_owned_resource.has_value()) {
      return std::nullopt;
    }
    return m_owned_resource->failed_allocations();
  }

  iterator begin() noexcept
  {
    return m_values.begin();
  }

  const_iterator begin() const noexcept
  {
    return m_values.begin();
  }

  const_iterator cbegin() const noexcept
  {
    return m_values.cbegin();
  }

  iterator end() noexcept
  {
    return m_values.end();
  }

  const_iterator end() const noexcept
  {
    return m_values.end();
  }

  const_iterator cend() const noexcept
  {
    return m_values.cend();
  }

  reverse_iterator rbegin() noexcept
  {
    return m_values.rbegin();
  }

  const_reverse_iterator rbegin() const noexcept
  {
    return m_values.rbegin();
  }

  const_reverse_iterator crbegin() const noexcept
  {
    return m_values.crbegin();
  }

  reverse_iterator rend() noexcept
  {
    return m_values.rend();
  }

  const_reverse_iterator rend() const noexcept
  {
    return m_values.rend();
  }

  const_reverse_iterator crend() const noexcept
  {
    return m_values.crend();
  }

private:
  static size_type calculate_max_bytes(const size_type max_elements)
  {
    validate_max_elements(max_elements);
    if (max_elements >
      std::numeric_limits<size_type>::max() / sizeof(value_t))
    {
      throw std::length_error("Strict vector capacity exceeds byte limit");
    }
    return max_elements * sizeof(value_t);
  }

  static size_type validate_max_elements(const size_type max_elements)
  {
    if (max_elements == 0U) {
      throw std::invalid_argument("Strict vector capacity must be positive");
    }
    return max_elements;
  }

  std::optional<memory_resource_c> m_owned_resource;
  std::pmr::vector<value_t> m_values;
  const size_type m_max_elements;
};

}  // namespace common
