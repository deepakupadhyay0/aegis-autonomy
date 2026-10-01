#pragma once

#include "accelerator/cuda/operation_context.hpp"
#include "accelerator/cuda/parallel_for.cuh"
#include "accelerator/cuda/operation_validation.hpp"
#include "accelerator/operation.hpp"
#include "accelerator/status.hpp"
#include "common/numeric_types.hpp"

#include <cuda_runtime.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

namespace accelerator::cuda
{

namespace detail
{

template<typename value_t>
struct runtime_rank_tensor_accessor_s
{
  value_t * data{nullptr};
  std::size_t extents[MAX_OPERATION_TENSOR_RANK]{};
  std::size_t strides[MAX_OPERATION_TENSOR_RANK]{};
  std::size_t rank{0U};

  __device__ std::size_t offset(const std::size_t logical_index) const noexcept
  {
    std::size_t remainder = logical_index;
    std::size_t memory_offset = 0U;
    for (std::size_t remaining_dimension = rank;
      remaining_dimension > 0U; --remaining_dimension)
    {
      const std::size_t dimension = remaining_dimension - 1U;
      const std::size_t coordinate = remainder % extents[dimension];
      remainder /= extents[dimension];
      memory_offset += coordinate * strides[dimension];
    }
    return memory_offset;
  }
};

template<typename value_t>
runtime_rank_tensor_accessor_s<value_t> make_runtime_rank_tensor_accessor(
  const operation_argument_s & argument) noexcept
{
  runtime_rank_tensor_accessor_s<value_t> accessor{};
  accessor.data = static_cast<value_t *>(argument.data);
  accessor.rank = argument.rank;
  for (std::size_t dimension = 0U; dimension < argument.rank; ++dimension) {
    accessor.extents[dimension] = argument.extents[dimension];
    accessor.strides[dimension] = argument.strides[dimension];
  }
  return accessor;
}

template<typename value_t, typename scalar_operation_t>
status_e enqueue_elementwise_binary(
  const operation_argument_s & left,
  const operation_argument_s & right,
  const operation_argument_s & result,
  const operation_context_s & context,
  const scalar_operation_t operation) noexcept
{
  const std::size_t element_count = result.element_count;

  if (left.contiguous() && right.contiguous() && result.contiguous()) {
    const value_t * const left_data = static_cast<const value_t *>(left.data);
    const value_t * const right_data = static_cast<const value_t *>(right.data);
    value_t * const result_data = static_cast<value_t *>(result.data);
    return launch_parallel_for(
      element_count,
      context,
      [left_data, right_data, result_data, operation]
      __device__(const std::size_t index) {
        result_data[index] = operation(left_data[index], right_data[index]);
      });
  }

  const runtime_rank_tensor_accessor_s<const value_t> left_accessor =
    make_runtime_rank_tensor_accessor<const value_t>(left);
  const runtime_rank_tensor_accessor_s<const value_t> right_accessor =
    make_runtime_rank_tensor_accessor<const value_t>(right);
  const runtime_rank_tensor_accessor_s<value_t> result_accessor =
    make_runtime_rank_tensor_accessor<value_t>(result);
  return launch_parallel_for(
    element_count,
    context,
    [left_accessor, right_accessor, result_accessor, operation]
    __device__(const std::size_t logical_index) {
      const std::size_t left_offset = left_accessor.offset(logical_index);
      const std::size_t right_offset = right_accessor.offset(logical_index);
      const std::size_t result_offset = result_accessor.offset(logical_index);
      result_accessor.data[result_offset] = operation(
        left_accessor.data[left_offset], right_accessor.data[right_offset]);
    });
}

template<typename scalar_operation_t>
class elementwise_binary_provider_c final : public operation_provider_i
{
public:
  elementwise_binary_provider_c(
    const operation_context_s & context,
    scalar_operation_t operation) noexcept
  : m_context(context),
    m_operation(std::move(operation))
  {
  }

  ~elementwise_binary_provider_c() noexcept override = default;

  status_e enqueue(
    const std::span<const operation_argument_s> arguments) noexcept override
  {
    const status_e validation =
      validate_numeric_arguments<3U>(arguments, m_context.device_index);
    if (validation != status_e::success) {
      return validation;
    }

    const operation_argument_s & left = arguments[0U];
    const operation_argument_s & right = arguments[1U];
    const operation_argument_s & result = arguments[2U];
    if (!result.writable) {
      return status_e::invalid_argument;
    }
    if (!same_shape(arguments)) {
      return status_e::shape_mismatch;
    }
    if (result.element_count == 0U) {
      return status_e::success;
    }
    if (!result.non_overlapping() ||
      unsafe_output_alias(left, result) ||
      unsafe_output_alias(right, result))
    {
      return status_e::overlapping_buffers;
    }
    if (cudaSetDevice(static_cast<int>(m_context.device_index)) != cudaSuccess) {
      return status_e::backend_error;
    }

    return dispatch_numeric(
      result.value_type,
      [&]<typename value_t>() noexcept {
        return enqueue_elementwise_binary<value_t>(
          left, right, result, m_context, m_operation);
      });
  }

private:
  operation_context_s m_context;
  scalar_operation_t m_operation;
};

}  // namespace detail

template<typename scalar_operation_t>
std::unique_ptr<operation_provider_i> make_elementwise_binary_provider(
  const operation_context_s & context,
  scalar_operation_t operation)
{
  static_assert(std::is_trivially_copyable_v<scalar_operation_t>);
  return std::make_unique<detail::elementwise_binary_provider_c<scalar_operation_t>>(
    context, std::move(operation));
}

template<typename scalar_operation_t>
class elementwise_binary_factory_c final
{
public:
  explicit elementwise_binary_factory_c(scalar_operation_t operation) noexcept
  : m_operation(std::move(operation))
  {
  }

  std::unique_ptr<operation_provider_i> operator()(
    const operation_context_s & context) const
  {
    return make_elementwise_binary_provider(context, m_operation);
  }

private:
  scalar_operation_t m_operation;
};

template<typename scalar_operation_t>
elementwise_binary_factory_c<scalar_operation_t> elementwise_binary(
  scalar_operation_t operation) noexcept
{
  return elementwise_binary_factory_c<scalar_operation_t>{std::move(operation)};
}

}  // namespace accelerator::cuda
