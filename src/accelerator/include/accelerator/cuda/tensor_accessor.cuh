#pragma once

#include "accelerator/operation.hpp"
#include "accelerator/status.hpp"
#include "common/numeric_types.hpp"

#include <cuda_runtime.h>

#include <array>
#include <cstddef>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>

namespace accelerator::cuda
{

namespace detail
{

template<typename accessor_t>
accessor_t make_tensor_accessor(
  const operation_argument_s & argument) noexcept;

}  // namespace detail

template<accelerator_element element_t, std::size_t rank_v>
class tensor_accessor_c final
{
public:
  using element_type = element_t;
  using value_type = std::remove_const_t<element_t>;
  static constexpr std::size_t RANK = rank_v;

  static_assert(rank_v > 0U);
  static_assert(rank_v <= MAX_OPERATION_TENSOR_RANK);

  __host__ __device__ constexpr std::size_t extent(
    const std::size_t dimension) const noexcept
  {
    return m_extents[dimension];
  }

  __host__ __device__ constexpr std::size_t size() const noexcept
  {
    return m_element_count;
  }

  __device__ element_t & operator[](const std::size_t logical_index) const noexcept
  {
    return m_data[offset(logical_index)];
  }

  template<typename ... coordinate_t>
  __device__ element_t & at(coordinate_t ... coordinates) const noexcept
  {
    static_assert(sizeof...(coordinate_t) == rank_v);
    const std::size_t coordinate_values[rank_v]{
      static_cast<std::size_t>(coordinates)...};
    std::size_t memory_offset = 0U;
    for (std::size_t dimension = 0U; dimension < rank_v; ++dimension) {
      memory_offset += coordinate_values[dimension] * m_strides[dimension];
    }
    return m_data[memory_offset];
  }

private:
  template<typename accessor_t>
  friend accessor_t detail::make_tensor_accessor(
    const operation_argument_s & argument) noexcept;

  __device__ std::size_t offset(const std::size_t logical_index) const noexcept
  {
    std::size_t remainder = logical_index;
    std::size_t memory_offset = 0U;
    for (std::size_t remaining_dimension = rank_v;
      remaining_dimension > 0U; --remaining_dimension)
    {
      const std::size_t dimension = remaining_dimension - 1U;
      const std::size_t coordinate = remainder % m_extents[dimension];
      remainder /= m_extents[dimension];
      memory_offset += coordinate * m_strides[dimension];
    }
    return memory_offset;
  }

  element_t * m_data{nullptr};
  std::size_t m_extents[rank_v]{};
  std::size_t m_strides[rank_v]{};
  std::size_t m_element_count{0U};
};

namespace detail
{

template<typename accessor_t>
status_e validate_tensor_argument(
  const operation_argument_s & argument,
  const common::uint32_t device_index) noexcept
{
  using value_t = typename accessor_t::value_type;
  if (argument.rank != accessor_t::RANK) {
    return status_e::invalid_argument;
  }
  if (argument.element_size != sizeof(value_t) ||
    argument.value_type != operation_value_type_v<value_t>)
  {
    return status_e::invalid_argument;
  }
  if constexpr (!std::is_const_v<typename accessor_t::element_type>) {
    if (!argument.writable) {
      return status_e::invalid_argument;
    }
  }
  if (argument.device_index != device_index) {
    return status_e::device_mismatch;
  }

  std::size_t element_count = 1U;
  for (std::size_t dimension = 0U; dimension < argument.rank; ++dimension) {
    const std::size_t extent = argument.extents[dimension];
    if (extent != 0U &&
      element_count > std::numeric_limits<std::size_t>::max() / extent)
    {
      return status_e::dimension_out_of_range;
    }
    element_count *= extent;
  }
  if (element_count != argument.element_count ||
    (element_count != 0U &&
    (argument.data == nullptr || argument.allocation_data == nullptr)))
  {
    return status_e::invalid_argument;
  }
  return status_e::success;
}

template<typename ... accessor_t, std::size_t ... index_v>
status_e validate_tensor_arguments(
  const std::span<const operation_argument_s> arguments,
  const common::uint32_t device_index,
  std::index_sequence<index_v ...>) noexcept
{
  if (arguments.size() != sizeof...(accessor_t)) {
    return status_e::invalid_argument;
  }
  const std::array<status_e, sizeof...(accessor_t)> statuses{
    validate_tensor_argument<accessor_t>(arguments[index_v], device_index)...};
  for (const status_e status : statuses) {
    if (status != status_e::success) {
      return status;
    }
  }
  return status_e::success;
}

template<typename ... accessor_t>
status_e validate_tensor_arguments(
  const std::span<const operation_argument_s> arguments,
  const common::uint32_t device_index) noexcept
{
  return validate_tensor_arguments<accessor_t ...>(
    arguments, device_index, std::index_sequence_for<accessor_t ...>{});
}

template<typename accessor_t>
accessor_t make_tensor_accessor(
  const operation_argument_s & argument) noexcept
{
  accessor_t accessor{};
  accessor.m_data = static_cast<typename accessor_t::element_type *>(argument.data);
  accessor.m_element_count = argument.element_count;
  for (std::size_t dimension = 0U; dimension < accessor_t::RANK; ++dimension) {
    accessor.m_extents[dimension] = argument.extents[dimension];
    accessor.m_strides[dimension] = argument.strides[dimension];
  }
  return accessor;
}

}  // namespace detail

}  // namespace accelerator::cuda
