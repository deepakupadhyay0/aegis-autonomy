#pragma once

#include "accelerator/cuda/operation_context.hpp"
#include "accelerator/cuda/operation_validation.hpp"
#include "accelerator/cuda/parallel_for.cuh"
#include "accelerator/cuda/tensor_accessor.cuh"
#include "accelerator/operation.hpp"
#include "accelerator/status.hpp"
#include "common/numeric_types.hpp"

#include <cuda_runtime.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace accelerator::cuda
{

namespace detail
{

template<typename accumulator_t, typename transform_t, typename reduce_t,
  typename ... accessor_t>
__global__ void transform_reduce_blocks_kernel(
  const std::size_t work_item_count,
  const accumulator_t identity,
  const transform_t transform,
  const reduce_t reduce,
  accumulator_t * const block_results,
  const accessor_t ... accessors)
{
  extern __shared__ __align__(16) unsigned char shared_bytes[];
  accumulator_t * const shared = reinterpret_cast<accumulator_t *>(shared_bytes);
  const std::size_t thread_index =
    static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
  const std::size_t grid_stride =
    static_cast<std::size_t>(blockDim.x) * gridDim.x;

  accumulator_t accumulator = identity;
  for (std::size_t index = thread_index; index < work_item_count; index += grid_stride) {
    accumulator = reduce(accumulator, transform(index, accessors...));
  }
  shared[threadIdx.x] = accumulator;
  __syncthreads();

  for (std::size_t stride = blockDim.x / 2U; stride > 0U; stride /= 2U) {
    if (threadIdx.x < stride) {
      shared[threadIdx.x] = reduce(shared[threadIdx.x], shared[threadIdx.x + stride]);
    }
    __syncthreads();
  }
  if (threadIdx.x == 0U) {
    block_results[blockIdx.x] = shared[0U];
  }
}

template<typename accumulator_t, typename reduce_t>
__global__ void reduce_block_results_kernel(
  const accumulator_t * const block_results,
  const std::size_t block_result_count,
  const accumulator_t identity,
  const reduce_t reduce,
  accumulator_t * const result)
{
  extern __shared__ __align__(16) unsigned char shared_bytes[];
  accumulator_t * const shared = reinterpret_cast<accumulator_t *>(shared_bytes);

  accumulator_t accumulator = identity;
  for (std::size_t index = threadIdx.x;
    index < block_result_count; index += blockDim.x)
  {
    accumulator = reduce(accumulator, block_results[index]);
  }
  shared[threadIdx.x] = accumulator;
  __syncthreads();

  for (std::size_t stride = blockDim.x / 2U; stride > 0U; stride /= 2U) {
    if (threadIdx.x < stride) {
      shared[threadIdx.x] = reduce(shared[threadIdx.x], shared[threadIdx.x + stride]);
    }
    __syncthreads();
  }
  if (threadIdx.x == 0U) {
    result[0U] = shared[0U];
  }
}

template<typename accumulator_t>
status_e validate_reduction_buffer(
  const operation_argument_s & argument,
  const common::uint32_t device_index,
  const bool require_element) noexcept
{
  if (argument.rank != 1U || !argument.contiguous() ||
    argument.element_size != sizeof(accumulator_t) ||
    argument.value_type != operation_value_type_v<accumulator_t> ||
    !argument.writable)
  {
    return status_e::invalid_argument;
  }
  if (argument.device_index != device_index) {
    return status_e::device_mismatch;
  }
  if (require_element && argument.element_count == 0U) {
    return status_e::invalid_argument;
  }
  if (argument.element_count != 0U &&
    (argument.data == nullptr || argument.allocation_data == nullptr))
  {
    return status_e::invalid_argument;
  }
  return status_e::success;
}

template<typename accumulator_t>
common::uint32_t reduction_thread_count(
  const operation_context_s & context) noexcept
{
  if (context.threads_per_block == 0U || context.shared_memory_per_block == 0U) {
    return 0U;
  }
  const std::size_t maximum_by_shared_memory =
    context.shared_memory_per_block / sizeof(accumulator_t);
  const std::size_t maximum_thread_count = std::min<std::size_t>(
    context.threads_per_block, maximum_by_shared_memory);

  common::uint32_t thread_count = 1U;
  while (static_cast<std::size_t>(thread_count) * 2U <= maximum_thread_count) {
    thread_count *= 2U;
  }
  return maximum_thread_count == 0U ? 0U : thread_count;
}

template<typename accumulator_t, typename transform_t, typename reduce_t,
  typename ... accessor_t>
status_e launch_transform_reduce(
  const std::size_t work_item_count,
  const operation_context_s & context,
  const accumulator_t identity,
  const transform_t transform,
  const reduce_t reduce,
  accumulator_t * const workspace,
  const std::size_t workspace_element_count,
  accumulator_t * const result,
  const accessor_t ... accessors) noexcept
{
  static_assert(alignof(accumulator_t) <= 16U);
  const common::uint32_t thread_count =
    reduction_thread_count<accumulator_t>(context);
  if (thread_count == 0U || context.stream == nullptr || result == nullptr) {
    return status_e::invalid_argument;
  }

  common::uint32_t block_count = 0U;
  if (work_item_count != 0U) {
    if (workspace == nullptr || workspace_element_count == 0U ||
      context.maximum_block_count == 0U)
    {
      return status_e::invalid_argument;
    }
    const std::size_t required_block_count =
      work_item_count / thread_count +
      (work_item_count % thread_count != 0U ? 1U : 0U);
    const std::size_t available_block_count = std::min<std::size_t>(
      workspace_element_count, context.maximum_block_count);
    block_count = static_cast<common::uint32_t>(
      std::min(required_block_count, available_block_count));
  }

  cudaStream_t const stream = reinterpret_cast<cudaStream_t>(context.stream);
  const std::size_t shared_memory_size =
    static_cast<std::size_t>(thread_count) * sizeof(accumulator_t);
  if (block_count != 0U) {
    transform_reduce_blocks_kernel<<<
      block_count, thread_count, shared_memory_size, stream>>>(
      work_item_count,
      identity,
      transform,
      reduce,
      workspace,
      accessors...);
    if (cudaGetLastError() != cudaSuccess) {
      return status_e::backend_error;
    }
  }

  reduce_block_results_kernel<<<1U, thread_count, shared_memory_size, stream>>>(
    workspace,
    block_count,
    identity,
    reduce,
    result);
  return cudaGetLastError() == cudaSuccess ?
         status_e::success : status_e::backend_error;
}

template<typename accumulator_t, typename transform_t, typename reduce_t,
  typename ... accessor_t>
class transform_reduce_provider_c final : public operation_provider_i
{
public:
  transform_reduce_provider_c(
    const operation_context_s & context,
    const work_dimensions_s & work_dimensions,
    const accumulator_t & identity,
    transform_t transform,
    reduce_t reduce) noexcept
  : m_context(context),
    m_work_dimensions(work_dimensions),
    m_identity(identity),
    m_transform(std::move(transform)),
    m_reduce(std::move(reduce))
  {
  }

  ~transform_reduce_provider_c() noexcept override = default;

  status_e enqueue(
    const std::span<const operation_argument_s> arguments) noexcept override
  {
    constexpr std::size_t INPUT_COUNT = sizeof...(accessor_t);
    constexpr std::size_t EXPECTED_COUNT = INPUT_COUNT + 2U;
    if (arguments.size() != EXPECTED_COUNT ||
      m_work_dimensions.argument_index >= INPUT_COUNT)
    {
      return status_e::invalid_argument;
    }

    const status_e input_validation = validate_tensor_arguments<accessor_t ...>(
      arguments.first(INPUT_COUNT), m_context.device_index);
    if (input_validation != status_e::success) {
      return input_validation;
    }

    const operation_argument_s & workspace = arguments[INPUT_COUNT];
    const operation_argument_s & result = arguments[INPUT_COUNT + 1U];
    const status_e workspace_validation = validate_reduction_buffer<accumulator_t>(
      workspace, m_context.device_index, false);
    if (workspace_validation != status_e::success) {
      return workspace_validation;
    }
    const status_e result_validation = validate_reduction_buffer<accumulator_t>(
      result, m_context.device_index, true);
    if (result_validation != status_e::success) {
      return result_validation;
    }
    if (same_allocation(workspace, result)) {
      return status_e::overlapping_buffers;
    }
    for (std::size_t index = 0U; index < INPUT_COUNT; ++index) {
      if (same_allocation(arguments[index], workspace) ||
        same_allocation(arguments[index], result))
      {
        return status_e::overlapping_buffers;
      }
    }

    const std::optional<std::size_t> work_item_count =
      try_work_item_count(arguments.first(INPUT_COUNT), m_work_dimensions);
    if (!work_item_count.has_value()) {
      return status_e::invalid_argument;
    }
    if (cudaSetDevice(static_cast<int>(m_context.device_index)) != cudaSuccess) {
      return status_e::backend_error;
    }

    return launch(
      *work_item_count,
      arguments,
      workspace,
      result,
      std::index_sequence_for<accessor_t ...>{});
  }

private:
  template<std::size_t ... index_v>
  status_e launch(
    const std::size_t work_item_count,
    const std::span<const operation_argument_s> arguments,
    const operation_argument_s & workspace,
    const operation_argument_s & result,
    std::index_sequence<index_v ...>) noexcept
  {
    return launch_transform_reduce(
      work_item_count,
      m_context,
      m_identity,
      m_transform,
      m_reduce,
      static_cast<accumulator_t *>(workspace.data),
      workspace.element_count,
      static_cast<accumulator_t *>(result.data),
      make_tensor_accessor<accessor_t>(arguments[index_v])...);
  }

  operation_context_s m_context;
  work_dimensions_s m_work_dimensions;
  accumulator_t m_identity;
  transform_t m_transform;
  reduce_t m_reduce;
};

}  // namespace detail

template<typename accumulator_t, typename transform_t, typename reduce_t,
  typename ... accessor_t>
class transform_reduce_factory_c final
{
public:
  transform_reduce_factory_c(
    const work_dimensions_s & work_dimensions,
    const accumulator_t & identity,
    transform_t transform,
    reduce_t reduce) noexcept
  : m_work_dimensions(work_dimensions),
    m_identity(identity),
    m_transform(std::move(transform)),
    m_reduce(std::move(reduce))
  {
  }

  std::unique_ptr<operation_provider_i> operator()(
    const operation_context_s & context) const
  {
    static_assert(accelerator_value<accumulator_t>);
    static_assert(sizeof...(accessor_t) > 0U);
    static_assert((std::is_trivially_copyable_v<accessor_t> && ...));
    static_assert(std::is_trivially_copyable_v<transform_t>);
    static_assert(std::is_trivially_copyable_v<reduce_t>);
    return std::make_unique<detail::transform_reduce_provider_c<
      accumulator_t, transform_t, reduce_t, accessor_t ...>>(
      context, m_work_dimensions, m_identity, m_transform, m_reduce);
  }

private:
  work_dimensions_s m_work_dimensions;
  accumulator_t m_identity;
  transform_t m_transform;
  reduce_t m_reduce;
};

/// Creates a two-stage transform-reduction provider factory.
///
/// execute() arguments are the typed inputs followed by a rank-one accumulator workspace and a
/// rank-one result. The reducer must be associative and accept the supplied identity.
template<typename accumulator_t, typename ... accessor_t,
  typename transform_t, typename reduce_t>
transform_reduce_factory_c<accumulator_t, transform_t, reduce_t, accessor_t ...>
transform_reduce(
  const work_dimensions_s & work_dimensions,
  const accumulator_t & identity,
  transform_t transform,
  reduce_t reduce) noexcept
{
  return transform_reduce_factory_c<
    accumulator_t, transform_t, reduce_t, accessor_t ...>{
    work_dimensions,
    identity,
    std::move(transform),
    std::move(reduce)};
}

}  // namespace accelerator::cuda
