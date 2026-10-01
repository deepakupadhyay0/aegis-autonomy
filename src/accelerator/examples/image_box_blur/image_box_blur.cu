#include "image_box_blur.hpp"

#include "accelerator/cuda/parallel_for.cuh"
#include "accelerator/cuda/tensor_accessor.cuh"

#include <cuda_runtime.h>

#include <cstddef>
#include <memory>

namespace accelerator_examples::image_box_blur
{

namespace
{

using input_accessor_t =
  accelerator::cuda::tensor_accessor_c<const scalar_t, 3U>;
using output_accessor_t =
  accelerator::cuda::tensor_accessor_c<scalar_t, 3U>;

std::unique_ptr<accelerator::operation_provider_i> make_box_blur_provider(
  const accelerator::cuda::operation_context_s & context)
{
  return accelerator::cuda::parallel_for<input_accessor_t, output_accessor_t>(
    accelerator::cuda::leading_dimensions(0U, 2U),
    [] __device__(
      const std::size_t pixel_index,
      const input_accessor_t input,
      const output_accessor_t output) {
      const std::size_t height = input.extent(0U);
      const std::size_t width = input.extent(1U);
      const std::size_t channel_count = input.extent(2U);
      const std::size_t row = pixel_index / width;
      const std::size_t column = pixel_index % width;
      const std::size_t first_row = row == 0U ? 0U : row - 1U;
      const std::size_t last_row =
        row + 1U < height ? row + 1U : height - 1U;
      const std::size_t first_column =
        column == 0U ? 0U : column - 1U;
      const std::size_t last_column =
        column + 1U < width ? column + 1U : width - 1U;

      for (std::size_t channel = 0U;
        channel < channel_count; ++channel)
      {
        scalar_t sum = 0.0F;
        std::size_t sample_count = 0U;
        for (std::size_t sample_row = first_row;
          sample_row <= last_row; ++sample_row)
        {
          for (std::size_t sample_column = first_column;
            sample_column <= last_column; ++sample_column)
          {
            sum += input.at(sample_row, sample_column, channel);
            ++sample_count;
          }
        }
        output.at(row, column, channel) =
          sum / static_cast<scalar_t>(sample_count);
      }
    })(context);
}

}  // namespace

image_box_blur_c::image_box_blur_c(const shape_t & image_shape)
: accelerator_c(accelerator::accelerator_config_s{}),
  m_box_blur(make_operation(make_box_blur_provider)),
  m_input(make_buffer<scalar_t>(image_shape)),
  m_output(make_buffer<scalar_t>(image_shape))
{
}

}  // namespace accelerator_examples::image_box_blur
