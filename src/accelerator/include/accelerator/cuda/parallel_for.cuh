#pragma once

#include "accelerator/cuda/operation_context.hpp"
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

/// Selects the leading dimensions of one operation argument as the logical work space.
/// `{0U, 1U}` produces N work items for an `{N, 3}` point tensor.
struct work_dimensions_s
{
  std::size_t argument_index{0U};
  std::size_t dimension_count{0U};
};

constexpr work_dimensions_s leading_dimensions(
  const std::size_t argument_index,
  const std::size_t dimension_count) noexcept
{
  return {argument_index, dimension_count};
}

namespace detail
{

inline std::optional<std::size_t> try_work_item_count(
  const std::span<const operation_argument_s> arguments,
  const work_dimensions_s & work_dimensions) noexcept
{
  if (work_dimensions.argument_index >= arguments.size() ||
    work_dimensions.dimension_count == 0U)
  {
    return std::nullopt;
  }

  const operation_argument_s & argument = arguments[work_dimensions.argument_index];
  if (work_dimensions.dimension_count > argument.rank) {
    return std::nullopt;
  }

  std::size_t work_item_count = 1U;
  for (std::size_t dimension = 0U;
    dimension < work_dimensions.dimension_count; ++dimension)
  {
    const std::size_t extent = argument.extents[dimension];
    if (extent != 0U &&
      work_item_count > std::numeric_limits<std::size_t>::max() / extent)
    {
      return std::nullopt;
    }
    work_item_count *= extent;
  }
  return work_item_count;
}

template<typename operation_t, typename ... accessor_t>
__global__ void parallel_for_kernel(
  const std::size_t work_item_count,
  const operation_t operation,
  const accessor_t ... accessors)
{
  const std::size_t thread_index =
    static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
  const std::size_t grid_stride =
    static_cast<std::size_t>(blockDim.x) * gridDim.x;

  for (std::size_t index = thread_index; index < work_item_count; index += grid_stride) {
    operation(index, accessors...);
  }
}

template<typename operation_t, typename ... accessor_t>
status_e launch_parallel_for(
  const std::size_t work_item_count,
  const operation_context_s & context,
  const operation_t operation,
  const accessor_t ... accessors) noexcept
{
  if (work_item_count == 0U) {
    return status_e::success;
  }
  if (context.stream == nullptr || context.threads_per_block == 0U ||
    context.maximum_block_count == 0U)
  {
    return status_e::invalid_argument;
  }

  const std::size_t required_block_count =
    work_item_count / context.threads_per_block +
    (work_item_count % context.threads_per_block != 0U ? 1U : 0U);
  const common::uint32_t block_count = static_cast<common::uint32_t>(
    std::min<std::size_t>(required_block_count, context.maximum_block_count));
  cudaStream_t const stream = reinterpret_cast<cudaStream_t>(context.stream);
  parallel_for_kernel<<<block_count, context.threads_per_block, 0U, stream>>>(
    work_item_count, operation, accessors...);
  return cudaGetLastError() == cudaSuccess ?
         status_e::success : status_e::backend_error;
}

template<typename operation_t, typename ... accessor_t>
class parallel_for_provider_c final : public operation_provider_i
{
public:
  parallel_for_provider_c(
    const operation_context_s & context,
    const work_dimensions_s & work_dimensions,
    operation_t operation) noexcept
  : m_context(context),
    m_work_dimensions(work_dimensions),
    m_operation(std::move(operation))
  {
  }

  ~parallel_for_provider_c() noexcept override = default;

  status_e enqueue(
    const std::span<const operation_argument_s> arguments) noexcept override
  {
    const status_e validation =
      validate_tensor_arguments<accessor_t ...>(arguments, m_context.device_index);
    if (validation != status_e::success) {
      return validation;
    }

    const std::optional<std::size_t> work_item_count =
      try_work_item_count(arguments, m_work_dimensions);
    if (!work_item_count.has_value()) {
      return status_e::invalid_argument;
    }
    if (cudaSetDevice(static_cast<int>(m_context.device_index)) != cudaSuccess) {
      return status_e::backend_error;
    }

    return launch(*work_item_count, arguments, std::index_sequence_for<accessor_t ...>{});
  }

private:
  template<std::size_t ... index_v>
  status_e launch(
    const std::size_t work_item_count,
    const std::span<const operation_argument_s> arguments,
    std::index_sequence<index_v ...>) noexcept
  {
    return launch_parallel_for(
      work_item_count,
      m_context,
      m_operation,
      make_tensor_accessor<accessor_t>(arguments[index_v])...);
  }

  operation_context_s m_context;
  work_dimensions_s m_work_dimensions;
  operation_t m_operation;
};

}  // namespace detail

template<typename operation_t, typename ... accessor_t>
class parallel_for_factory_c final
{
public:
  parallel_for_factory_c(
    const work_dimensions_s & work_dimensions,
    operation_t operation) noexcept
  : m_work_dimensions(work_dimensions),
    m_operation(std::move(operation))
  {
  }

  std::unique_ptr<operation_provider_i> operator()(
    const operation_context_s & context) const
  {
    static_assert(sizeof...(accessor_t) > 0U);
    static_assert(std::is_trivially_copyable_v<operation_t>);
    static_assert((std::is_trivially_copyable_v<accessor_t> && ...));
    return std::make_unique<detail::parallel_for_provider_c<operation_t, accessor_t ...>>(
      context, m_work_dimensions, m_operation);
  }

private:
  work_dimensions_s m_work_dimensions;
  operation_t m_operation;
};

/// Creates a provider factory for an indexed CUDA operation.
///
/// The operation receives (logical_index, accessors...). Launch geometry and grid-stride
/// iteration are handled by the pattern.
template<typename ... accessor_t, typename operation_t>
parallel_for_factory_c<operation_t, accessor_t ...> parallel_for(
  const work_dimensions_s & work_dimensions,
  operation_t operation) noexcept
{
  return parallel_for_factory_c<operation_t, accessor_t ...>{
    work_dimensions, std::move(operation)};
}

}  // namespace accelerator::cuda
