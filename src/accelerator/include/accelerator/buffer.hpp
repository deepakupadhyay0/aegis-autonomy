#pragma once

#include "accelerator/tensor.hpp"
#include "common/numeric_types.hpp"

#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace accelerator
{

class accelerator_c;

/// Move-only ownership of a contiguous device allocation and its tensor shape.
template<accelerator_value value_t, std::size_t rank_v>
class buffer_c final
{
public:
  buffer_c() noexcept = default;
  ~buffer_c() noexcept = default;

  buffer_c(const buffer_c &) = delete;
  buffer_c & operator=(const buffer_c &) = delete;
  buffer_c(buffer_c && other) noexcept
  : m_owner(std::move(other.m_owner)),
    m_data(std::exchange(other.m_data, nullptr)),
    m_shape(std::exchange(other.m_shape, tensor_shape_s<rank_v> {})),
    m_strides(std::exchange(other.m_strides, tensor_strides_s<rank_v> {})),
    m_element_count(std::exchange(other.m_element_count, 0U)),
    m_size_bytes(std::exchange(other.m_size_bytes, 0U)),
    m_device_index(std::exchange(other.m_device_index, INVALID_DEVICE_INDEX))
  {
  }

  buffer_c & operator=(buffer_c && other) noexcept
  {
    if (this != &other) {
      m_owner = std::move(other.m_owner);
      m_data = std::exchange(other.m_data, nullptr);
      m_shape = std::exchange(other.m_shape, tensor_shape_s<rank_v>{});
      m_strides = std::exchange(other.m_strides, tensor_strides_s<rank_v>{});
      m_element_count = std::exchange(other.m_element_count, 0U);
      m_size_bytes = std::exchange(other.m_size_bytes, 0U);
      m_device_index = std::exchange(other.m_device_index, INVALID_DEVICE_INDEX);
    }
    return *this;
  }

  constexpr const tensor_shape_s<rank_v> & shape() const noexcept
  {
    return m_shape;
  }

  constexpr const tensor_strides_s<rank_v> & strides() const noexcept
  {
    return m_strides;
  }

  constexpr std::size_t element_count() const noexcept
  {
    return m_element_count;
  }

  constexpr std::size_t size_bytes() const noexcept
  {
    return m_size_bytes;
  }

  constexpr bool empty() const noexcept
  {
    return m_element_count == 0U;
  }

  constexpr common::uint32_t device_index() const noexcept
  {
    return m_device_index;
  }

  device_tensor_view_c<value_t, rank_v> view() noexcept
  {
    return device_tensor_view_c<value_t, rank_v>(
      m_data, m_shape, m_strides, m_device_index, m_data);
  }

  device_tensor_view_c<const value_t, rank_v> view() const noexcept
  {
    return device_tensor_view_c<const value_t, rank_v>(
      m_data, m_shape, m_strides, m_device_index, m_data);
  }

  /// Creates a validated strided view into this allocation. The returned view borrows storage
  /// and must not outlive the buffer or pending operations using it.
  template<std::size_t view_rank_v>
  std::optional<device_tensor_view_c<value_t, view_rank_v>> try_view(
    const tensor_shape_s<view_rank_v> & shape,
    const tensor_strides_s<view_rank_v> & strides,
    const std::size_t element_offset = 0U) noexcept
  {
    const std::optional<std::size_t> required_elements =
      detail::try_required_elements(shape, strides);
    if (!required_elements.has_value() || element_offset > m_element_count ||
      *required_elements > m_element_count - element_offset)
    {
      return std::nullopt;
    }
    value_t * data = m_data;
    if (element_offset != 0U) {
      data += element_offset;
    }
    return device_tensor_view_c<value_t, view_rank_v>(
      data, shape, strides, m_device_index, m_data);
  }

  template<std::size_t view_rank_v>
  std::optional<device_tensor_view_c<const value_t, view_rank_v>> try_view(
    const tensor_shape_s<view_rank_v> & shape,
    const tensor_strides_s<view_rank_v> & strides,
    const std::size_t element_offset = 0U) const noexcept
  {
    const std::optional<std::size_t> required_elements =
      detail::try_required_elements(shape, strides);
    if (!required_elements.has_value() || element_offset > m_element_count ||
      *required_elements > m_element_count - element_offset)
    {
      return std::nullopt;
    }
    const value_t * data = m_data;
    if (element_offset != 0U) {
      data += element_offset;
    }
    return device_tensor_view_c<const value_t, view_rank_v>(
      data, shape, strides, m_device_index, m_data);
  }

private:
  friend class accelerator_c;

  buffer_c(
    std::shared_ptr<void> owner,
    value_t * const data,
    const tensor_shape_s<rank_v> & shape,
    const tensor_strides_s<rank_v> & strides,
    const std::size_t element_count,
    const std::size_t size_bytes,
    const common::uint32_t device_index) noexcept
  : m_owner(std::move(owner)),
    m_data(data),
    m_shape(shape),
    m_strides(strides),
    m_element_count(element_count),
    m_size_bytes(size_bytes),
    m_device_index(device_index)
  {
  }

  std::shared_ptr<void> m_owner;
  value_t * m_data{nullptr};
  tensor_shape_s<rank_v> m_shape{};
  tensor_strides_s<rank_v> m_strides{};
  std::size_t m_element_count{0U};
  std::size_t m_size_bytes{0U};
  static constexpr common::uint32_t INVALID_DEVICE_INDEX =
    std::numeric_limits<common::uint32_t>::max();
  common::uint32_t m_device_index{INVALID_DEVICE_INDEX};
};

}  // namespace accelerator
