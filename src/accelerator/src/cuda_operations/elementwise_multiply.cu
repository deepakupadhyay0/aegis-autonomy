#include "accelerator/cuda/elementwise_operation.cuh"
#include "accelerator/cuda/operations.hpp"

#include <cuda_runtime.h>

#include <memory>

namespace accelerator::cuda_operations
{

std::unique_ptr<operation_provider_i> make_elementwise_multiply_provider(
  const cuda::operation_context_s & context)
{
  return cuda::elementwise_binary(
    [] __device__(const auto left, const auto right) {
      return left * right;
    })(context);
}

}  // namespace accelerator::cuda_operations
