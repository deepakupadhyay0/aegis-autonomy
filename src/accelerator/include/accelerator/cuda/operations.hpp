#pragma once

#include "accelerator/cuda/operation_context.hpp"
#include "accelerator/operation.hpp"
#include "accelerator/visibility_control.hpp"

#include <memory>

namespace accelerator::cuda_operations
{

ACCELERATOR_PUBLIC std::unique_ptr<operation_provider_i>
make_elementwise_multiply_provider(const cuda::operation_context_s & context);

ACCELERATOR_PUBLIC std::unique_ptr<operation_provider_i>
make_matrix_multiply_provider(const cuda::operation_context_s & context);

}  // namespace accelerator::cuda_operations
