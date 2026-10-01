#pragma once

#include "accelerator/operation.hpp"
#include "accelerator/status.hpp"
#include "common/numeric_types.hpp"

#include <cstddef>
#include <limits>
#include <span>

namespace accelerator::cuda::detail
{

inline std::size_t value_size(const operation_value_type_e type) noexcept
{
  switch (type) {
    case operation_value_type_e::int8:
    case operation_value_type_e::uint8:
      return 1U;
    case operation_value_type_e::int16:
    case operation_value_type_e::uint16:
      return 2U;
    case operation_value_type_e::int32:
    case operation_value_type_e::uint32:
    case operation_value_type_e::float32:
      return 4U;
    case operation_value_type_e::int64:
    case operation_value_type_e::uint64:
    case operation_value_type_e::float64:
      return 8U;
    case operation_value_type_e::unknown:
    default:
      return 0U;
  }
}

inline bool valid_numeric_tensor(const operation_argument_s & argument) noexcept
{
  if (argument.rank == 0U || argument.rank > MAX_OPERATION_TENSOR_RANK ||
    argument.element_size != value_size(argument.value_type))
  {
    return false;
  }

  std::size_t element_count = 1U;
  for (std::size_t dimension = 0U; dimension < argument.rank; ++dimension) {
    const std::size_t extent = argument.extents[dimension];
    if (extent != 0U &&
      element_count > std::numeric_limits<std::size_t>::max() / extent)
    {
      return false;
    }
    element_count *= extent;
  }

  return element_count == argument.element_count &&
         (element_count == 0U ||
         (argument.data != nullptr && argument.allocation_data != nullptr));
}

template<std::size_t expected_count_v>
status_e validate_numeric_arguments(
  const std::span<const operation_argument_s> arguments,
  const common::uint32_t device_index) noexcept
{
  static_assert(expected_count_v > 0U);
  if (arguments.size() != expected_count_v) {
    return status_e::invalid_argument;
  }

  const operation_argument_s & first = arguments.front();
  for (const operation_argument_s & argument : arguments) {
    if (argument.rank > MAX_OPERATION_TENSOR_RANK) {
      return status_e::dimension_out_of_range;
    }
    if (!valid_numeric_tensor(argument) ||
      argument.value_type != first.value_type ||
      argument.element_size != first.element_size)
    {
      return status_e::invalid_argument;
    }
    if (argument.device_index != device_index) {
      return status_e::device_mismatch;
    }
  }
  return status_e::success;
}

inline bool same_shape(
  const operation_argument_s & left,
  const operation_argument_s & right) noexcept
{
  if (left.rank != right.rank || left.element_count != right.element_count) {
    return false;
  }
  for (std::size_t dimension = 0U; dimension < left.rank; ++dimension) {
    if (left.extents[dimension] != right.extents[dimension]) {
      return false;
    }
  }
  return true;
}

inline bool same_shape(
  const std::span<const operation_argument_s> arguments) noexcept
{
  if (arguments.empty()) {
    return false;
  }
  for (std::size_t index = 1U; index < arguments.size(); ++index) {
    if (!same_shape(arguments.front(), arguments[index])) {
      return false;
    }
  }
  return true;
}

inline bool rank_is(
  const std::span<const operation_argument_s> arguments,
  const std::size_t expected_rank) noexcept
{
  for (const operation_argument_s & argument : arguments) {
    if (argument.rank != expected_rank) {
      return false;
    }
  }
  return true;
}

inline bool all_contiguous(
  const std::span<const operation_argument_s> arguments) noexcept
{
  for (const operation_argument_s & argument : arguments) {
    if (!argument.contiguous()) {
      return false;
    }
  }
  return true;
}

inline bool same_allocation(
  const operation_argument_s & left,
  const operation_argument_s & right) noexcept
{
  return left.allocation_data != nullptr &&
         left.allocation_data == right.allocation_data;
}

inline bool identical_view(
  const operation_argument_s & left,
  const operation_argument_s & right) noexcept
{
  if (left.data != right.data || !same_shape(left, right)) {
    return false;
  }
  for (std::size_t dimension = 0U; dimension < left.rank; ++dimension) {
    if (left.strides[dimension] != right.strides[dimension]) {
      return false;
    }
  }
  return true;
}

inline bool unsafe_output_alias(
  const operation_argument_s & input,
  const operation_argument_s & output) noexcept
{
  return same_allocation(input, output) && !identical_view(input, output);
}

template<typename operation_t>
status_e dispatch_numeric(
  const operation_value_type_e type,
  operation_t && operation) noexcept
{
  switch (type) {
    case operation_value_type_e::int8:
      return operation.template operator()<common::int8_t>();
    case operation_value_type_e::int16:
      return operation.template operator()<common::int16_t>();
    case operation_value_type_e::int32:
      return operation.template operator()<common::int32_t>();
    case operation_value_type_e::int64:
      return operation.template operator()<common::int64_t>();
    case operation_value_type_e::uint8:
      return operation.template operator()<common::uint8_t>();
    case operation_value_type_e::uint16:
      return operation.template operator()<common::uint16_t>();
    case operation_value_type_e::uint32:
      return operation.template operator()<common::uint32_t>();
    case operation_value_type_e::uint64:
      return operation.template operator()<common::uint64_t>();
    case operation_value_type_e::float32:
      return operation.template operator()<common::float32_t>();
    case operation_value_type_e::float64:
      return operation.template operator()<common::float64_t>();
    case operation_value_type_e::unknown:
    default:
      return status_e::invalid_argument;
  }
}

template<typename operation_t>
status_e dispatch_floating(
  const operation_value_type_e type,
  operation_t && operation) noexcept
{
  switch (type) {
    case operation_value_type_e::float32:
      return operation.template operator()<common::float32_t>();
    case operation_value_type_e::float64:
      return operation.template operator()<common::float64_t>();
    default:
      return status_e::invalid_argument;
  }
}

}  // namespace accelerator::cuda::detail
