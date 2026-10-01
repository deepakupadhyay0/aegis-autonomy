#pragma once

#include "common/numeric_types.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>

namespace accelerator
{

class accelerator_c;

template<typename value_t>
inline constexpr bool is_accelerator_value_v =
  !std::is_const_v<value_t>&&
  !std::is_volatile_v<value_t>&&
  !std::is_pointer_v<value_t>&&
  std::is_trivially_copyable_v<value_t>&&
  std::is_standard_layout_v<value_t>;

template<typename value_t>
concept accelerator_value = is_accelerator_value_v<value_t>;

template<typename element_t>
concept accelerator_element =
  !std::is_volatile_v<element_t>&&
  accelerator_value<std::remove_const_t<element_t>>;

template<accelerator_value value_t, std::size_t rank_v>
class buffer_c;

template<std::size_t rank_v>
struct tensor_shape_s
{
  static_assert(rank_v > 0U, "A tensor rank must be greater than zero");

  std::array<std::size_t, rank_v> extents{};

  constexpr std::optional<std::size_t> try_element_count() const noexcept
  {
    std::size_t count = 1U;
    for (const std::size_t extent : extents) {
      if (extent != 0U && count > std::numeric_limits<std::size_t>::max() / extent) {
        return std::nullopt;
      }
      count *= extent;
    }
    return count;
  }

  friend constexpr bool operator==(
    const tensor_shape_s & left,
    const tensor_shape_s & right) noexcept = default;
};

template<std::size_t rank_v>
struct tensor_strides_s
{
  static_assert(rank_v > 0U, "A tensor rank must be greater than zero");

  std::array<std::size_t, rank_v> elements{};

  friend constexpr bool operator==(
    const tensor_strides_s & left,
    const tensor_strides_s & right) noexcept = default;
};

namespace detail
{

template<std::size_t rank_v>
constexpr std::optional<tensor_strides_s<rank_v>>
try_make_contiguous_strides(const tensor_shape_s<rank_v> & shape) noexcept
{
  tensor_strides_s<rank_v> strides{};
  std::size_t stride = 1U;
  for (std::size_t dimension = rank_v; dimension > 0U; --dimension) {
    const std::size_t index = dimension - 1U;
    strides.elements[index] = stride;
    const std::size_t extent = shape.extents[index];
    if (extent != 0U && stride > std::numeric_limits<std::size_t>::max() / extent) {
      return std::nullopt;
    }
    stride *= extent;
  }
  return strides;
}

template<std::size_t rank_v>
constexpr bool is_contiguous(
  const tensor_shape_s<rank_v> & shape,
  const tensor_strides_s<rank_v> & strides) noexcept
{
  const std::optional<tensor_strides_s<rank_v>> expected =
    try_make_contiguous_strides(shape);
  return expected.has_value() && *expected == strides;
}

template<std::size_t rank_v>
constexpr std::optional<std::size_t> try_required_elements(
  const tensor_shape_s<rank_v> & shape,
  const tensor_strides_s<rank_v> & strides) noexcept
{
  const std::optional<std::size_t> element_count = shape.try_element_count();
  if (!element_count.has_value()) {
    return std::nullopt;
  }
  if (*element_count == 0U) {
    return 0U;
  }

  const std::size_t maximum = std::numeric_limits<std::size_t>::max();
  std::size_t last_offset = 0U;
  for (std::size_t dimension = 0U; dimension < rank_v; ++dimension) {
    const std::size_t extent = shape.extents[dimension];
    const std::size_t stride = strides.elements[dimension];
    if (stride == 0U || extent - 1U > maximum / stride) {
      return std::nullopt;
    }
    const std::size_t dimension_offset = (extent - 1U) * stride;
    if (last_offset > maximum - dimension_offset) {
      return std::nullopt;
    }
    last_offset += dimension_offset;
  }
  if (last_offset == maximum) {
    return std::nullopt;
  }
  return last_offset + 1U;
}

class device_tensor_view_access_c;

}  // namespace detail

/// Borrowed host tensor view. element_t carries constness. The storage owner
/// must outlive the view and every operation using it.
template<accelerator_element element_t, std::size_t rank_v>
class host_tensor_view_c final
{
public:
  using value_t = std::remove_const_t<element_t>;

  static std::optional<host_tensor_view_c> try_create_contiguous(
    const std::span<element_t> storage,
    const tensor_shape_s<rank_v> & shape) noexcept
  {
    const std::optional<std::size_t> element_count = shape.try_element_count();
    const std::optional<tensor_strides_s<rank_v>> strides =
      detail::try_make_contiguous_strides(shape);
    if (!element_count.has_value() || !strides.has_value() ||
      *element_count > storage.size())
    {
      return std::nullopt;
    }
    return host_tensor_view_c(storage, shape, *strides);
  }

  static std::optional<host_tensor_view_c> try_create(
    const std::span<element_t> storage,
    const tensor_shape_s<rank_v> & shape,
    const tensor_strides_s<rank_v> & strides) noexcept
  {
    const std::optional<std::size_t> required_elements =
      detail::try_required_elements(shape, strides);
    if (!required_elements.has_value() || *required_elements > storage.size()) {
      return std::nullopt;
    }
    return host_tensor_view_c(storage, shape, strides);
  }

  constexpr element_t * data() const noexcept
  {
    return m_data;
  }

  constexpr std::size_t storage_size() const noexcept
  {
    return m_storage_size;
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
    return m_shape.try_element_count().value_or(0U);
  }

  constexpr bool empty() const noexcept
  {
    return element_count() == 0U;
  }

  constexpr bool contiguous() const noexcept
  {
    return detail::is_contiguous(m_shape, m_strides);
  }

private:
  constexpr host_tensor_view_c(
    const std::span<element_t> storage,
    const tensor_shape_s<rank_v> & shape,
    const tensor_strides_s<rank_v> & strides) noexcept
  : m_data(storage.data()),
    m_storage_size(storage.size()),
    m_shape(shape),
    m_strides(strides)
  {
  }

  element_t * m_data;
  std::size_t m_storage_size;
  tensor_shape_s<rank_v> m_shape;
  tensor_strides_s<rank_v> m_strides;
};

/// Opaque, borrowed device tensor view. A device buffer constructs this view;
/// ordinary application code cannot manufacture one from a host pointer.
template<accelerator_element element_t, std::size_t rank_v>
class device_tensor_view_c final
{
public:
  using value_t = std::remove_const_t<element_t>;

  constexpr device_tensor_view_c() noexcept = default;

  template<accelerator_element other_element_t>
  constexpr device_tensor_view_c(
    const device_tensor_view_c<other_element_t, rank_v> & other) noexcept
  requires (
    std::is_const_v<element_t>&&
    !std::is_const_v<other_element_t>&&
    std::same_as<std::remove_const_t<element_t>,
    std::remove_const_t<other_element_t>>)
  : m_data(other.m_data),
    m_allocation_data(other.m_allocation_data),
    m_shape(other.m_shape),
    m_strides(other.m_strides),
    m_device_index(other.m_device_index)
  {
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
    return m_shape.try_element_count().value_or(0U);
  }

  constexpr bool empty() const noexcept
  {
    return element_count() == 0U;
  }

  constexpr common::uint32_t device_index() const noexcept
  {
    return m_device_index;
  }

private:
  friend class accelerator_c;
  template<accelerator_value, std::size_t>
  friend class buffer_c;
  template<accelerator_element, std::size_t>
  friend class device_tensor_view_c;
  friend class detail::device_tensor_view_access_c;

  constexpr device_tensor_view_c(
    element_t * const data,
    const tensor_shape_s<rank_v> & shape,
    const tensor_strides_s<rank_v> & strides,
    const common::uint32_t device_index,
    const void * const allocation_data) noexcept
  : m_data(data),
    m_allocation_data(allocation_data),
    m_shape(shape),
    m_strides(strides),
    m_device_index(device_index)
  {
  }

  element_t * m_data{nullptr};
  const void * m_allocation_data{nullptr};
  tensor_shape_s<rank_v> m_shape{};
  tensor_strides_s<rank_v> m_strides{};
  common::uint32_t m_device_index{
    std::numeric_limits<common::uint32_t>::max()};
};

}  // namespace accelerator
