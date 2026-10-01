#include "accelerator/operation.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <utility>

namespace accelerator
{

bool operation_argument_s::contiguous() const noexcept
{
  if (rank == 0U || rank > MAX_OPERATION_TENSOR_RANK) {
    return false;
  }

  std::size_t expected_stride = 1U;
  for (std::size_t remaining_dimension = rank;
    remaining_dimension > 0U; --remaining_dimension)
  {
    const std::size_t dimension = remaining_dimension - 1U;
    if (strides[dimension] != expected_stride) {
      return false;
    }
    if (extents[dimension] != 0U) {
      if (expected_stride >
        std::numeric_limits<std::size_t>::max() / extents[dimension])
      {
        return false;
      }
      expected_stride *= extents[dimension];
    }
  }
  return true;
}

bool operation_argument_s::non_overlapping() const noexcept
{
  if (rank == 0U || rank > MAX_OPERATION_TENSOR_RANK) {
    return false;
  }
  if (element_count == 0U) {
    return true;
  }

  struct dimension_s
  {
    std::size_t extent;
    std::size_t stride;
  };

  std::array<dimension_s, MAX_OPERATION_TENSOR_RANK> dimensions{};
  for (std::size_t dimension = 0U; dimension < rank; ++dimension) {
    dimensions[dimension] = {extents[dimension], strides[dimension]};
  }
  std::sort(
    dimensions.begin(),
    dimensions.begin() + static_cast<std::ptrdiff_t>(rank),
    [](const dimension_s & left, const dimension_s & right) {
      return left.stride < right.stride;
    });

  std::size_t occupied_span = 1U;
  for (std::size_t dimension = 0U; dimension < rank; ++dimension) {
    const dimension_s & current = dimensions[dimension];
    if (current.extent <= 1U) {
      continue;
    }
    if (current.stride < occupied_span) {
      return false;
    }
    if (current.extent - 1U >
      std::numeric_limits<std::size_t>::max() / current.stride)
    {
      return false;
    }
    const std::size_t additional_span = (current.extent - 1U) * current.stride;
    if (occupied_span >
      std::numeric_limits<std::size_t>::max() - additional_span)
    {
      return false;
    }
    occupied_span += additional_span;
  }
  return true;
}

operation_c::operation_c(
  std::unique_ptr<operation_provider_i> provider) noexcept
: m_provider(std::move(provider))
{
}

operation_c::~operation_c() noexcept = default;

}  // namespace accelerator
