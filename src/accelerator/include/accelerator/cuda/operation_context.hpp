#pragma once

#include "common/numeric_types.hpp"

#include <cstddef>

namespace accelerator::cuda
{

/// Borrowed CUDA resources supplied to an operation provider during accelerator construction.
///
/// The accelerator owns every handle. Providers borrow them and must be destroyed before the
/// accelerator; they may enqueue work on the stream but must not destroy or replace any handle. CUDA
/// translation units can convert the opaque handles to their native CUDA types; application-facing
/// C++ headers remain independent of CUDA headers.
struct operation_context_s
{
  void * stream{nullptr};
  void * linear_algebra_handle{nullptr};
  common::uint32_t device_index{0U};
  common::uint32_t threads_per_block{0U};
  common::uint32_t maximum_block_count{0U};
  std::size_t shared_memory_per_block{0U};
};

}  // namespace accelerator::cuda
