#include "cuda_pattern_test_accelerator.hpp"

#include "accelerator/cuda/parallel_for.cuh"
#include "accelerator/cuda/tensor_accessor.cuh"
#include "accelerator/cuda/transform_reduce.cuh"

#include <cuda_runtime.h>

#include <cstddef>
#include <memory>

namespace accelerator_test
{

namespace
{

using input_accessor_t =
  accelerator::cuda::tensor_accessor_c<const pattern_scalar_t, 1U>;
using output_accessor_t =
  accelerator::cuda::tensor_accessor_c<pattern_scalar_t, 1U>;

std::unique_ptr<accelerator::operation_provider_i> make_increment_provider(
  const accelerator::cuda::operation_context_s & context)
{
  return accelerator::cuda::parallel_for<input_accessor_t, output_accessor_t>(
    accelerator::cuda::leading_dimensions(0U, 1U),
    [] __device__(
      const std::size_t index,
      const input_accessor_t input,
      const output_accessor_t output) {
      output[index] = input[index] + 1.0F;
    })(context);
}

std::unique_ptr<accelerator::operation_provider_i> make_sum_provider(
  const accelerator::cuda::operation_context_s & context)
{
  return accelerator::cuda::transform_reduce<
    pattern_scalar_t, input_accessor_t>(
    accelerator::cuda::leading_dimensions(0U, 1U),
    0.0F,
    [] __device__(const std::size_t index, const input_accessor_t input) {
      return input[index];
    },
    [] __device__(
      const pattern_scalar_t left,
      const pattern_scalar_t right) {
      return left + right;
    })(context);
}

}  // namespace

cuda_pattern_test_accelerator_c::cuda_pattern_test_accelerator_c()
: accelerator_c(accelerator::accelerator_config_s{}),
  m_increment(make_operation(make_increment_provider)),
  m_sum(make_operation(make_sum_provider))
{
}

cuda_pattern_test_accelerator_c::~cuda_pattern_test_accelerator_c() noexcept = default;

accelerator::status_e cuda_pattern_test_accelerator_c::increment(
  const pattern_buffer_t & input,
  pattern_buffer_t & output) noexcept
{
  return m_increment.execute(input, output);
}

accelerator::status_e cuda_pattern_test_accelerator_c::increment_read_only_output(
  const pattern_buffer_t & input,
  const pattern_buffer_t & output) noexcept
{
  return m_increment.execute(input, output);
}

accelerator::status_e cuda_pattern_test_accelerator_c::sum(
  const pattern_buffer_t & input,
  pattern_buffer_t & workspace,
  pattern_buffer_t & result) noexcept
{
  return m_sum.execute(input, workspace, result);
}

}  // namespace accelerator_test
