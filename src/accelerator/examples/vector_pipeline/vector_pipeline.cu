#include "vector_pipeline.hpp"

#include "accelerator/cuda/parallel_for.cuh"
#include "accelerator/cuda/tensor_accessor.cuh"
#include "accelerator/cuda/transform_reduce.cuh"

#include <cuda_runtime.h>

#include <cstddef>
#include <memory>

namespace accelerator_examples::vector_pipeline
{

namespace
{

using input_accessor_t =
  accelerator::cuda::tensor_accessor_c<const scalar_t, 1U>;
using output_accessor_t =
  accelerator::cuda::tensor_accessor_c<scalar_t, 1U>;

std::unique_ptr<accelerator::operation_provider_i> make_scale_provider(
  const accelerator::cuda::operation_context_s & context,
  const scalar_t scale)
{
  return accelerator::cuda::parallel_for<input_accessor_t, output_accessor_t>(
    accelerator::cuda::leading_dimensions(0U, 1U),
    [scale] __device__(
      const std::size_t index,
      const input_accessor_t input,
      const output_accessor_t output) {
      output[index] = input[index] * scale;
    })(context);
}

std::unique_ptr<accelerator::operation_provider_i> make_sum_provider(
  const accelerator::cuda::operation_context_s & context)
{
  return accelerator::cuda::transform_reduce<scalar_t, input_accessor_t>(
    accelerator::cuda::leading_dimensions(0U, 1U),
    0.0F,
    [] __device__(const std::size_t index, const input_accessor_t input) {
      return input[index];
    },
    [] __device__(const scalar_t left, const scalar_t right) {
      return left + right;
    })(context);
}

}  // namespace

vector_pipeline_c::vector_pipeline_c(
  const std::size_t element_count,
  const scalar_t scale)
: accelerator_c(accelerator::accelerator_config_s{}),
  m_scale(make_operation(
      [scale](const accelerator::cuda::operation_context_s & context) {
        return make_scale_provider(context, scale);
      })),
  m_sum(make_operation(make_sum_provider)),
  m_input(make_buffer<scalar_t>(shape_t{{element_count}})),
  m_scaled(make_buffer<scalar_t>(shape_t{{element_count}})),
  m_workspace(make_buffer<scalar_t>(shape_t{{WORKSPACE_CAPACITY}})),
  m_result(make_buffer<scalar_t>(shape_t{{1U}}))
{
}

}  // namespace accelerator_examples::vector_pipeline
