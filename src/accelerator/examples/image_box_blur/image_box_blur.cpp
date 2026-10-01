#include "image_box_blur.hpp"

#include <array>
#include <cstddef>
#include <exception>
#include <iostream>
#include <optional>
#include <utility>

namespace accelerator_examples::image_box_blur
{

image_box_blur_c::~image_box_blur_c() noexcept = default;

accelerator::status_e image_box_blur_c::process(
  const std::span<const scalar_t> input,
  const std::span<scalar_t> output,
  const shape_t & image_shape) noexcept
{
  using host_input_t =
    accelerator::host_tensor_view_c<const scalar_t, 3U>;
  using host_output_t = accelerator::host_tensor_view_c<scalar_t, 3U>;

  const std::optional<host_input_t> input_view =
    host_input_t::try_create_contiguous(input, image_shape);
  const std::optional<host_output_t> output_view =
    host_output_t::try_create_contiguous(output, image_shape);
  if (!input_view.has_value() || !output_view.has_value() ||
    image_shape != m_input.shape() || image_shape != m_output.shape())
  {
    return accelerator::status_e::shape_mismatch;
  }

  accelerator::status_e status = upload(*input_view, m_input);
  if (status != accelerator::status_e::success) {
    return status;
  }
  status = m_box_blur.execute(std::as_const(m_input), m_output);
  if (status != accelerator::status_e::success) {
    return status;
  }
  return download(m_output, *output_view);
}

}  // namespace accelerator_examples::image_box_blur

int main()
{
  using accelerator_examples::image_box_blur::image_box_blur_c;
  using accelerator_examples::image_box_blur::scalar_t;
  using accelerator_examples::image_box_blur::shape_t;

  constexpr std::size_t IMAGE_HEIGHT = 3U;
  constexpr std::size_t IMAGE_WIDTH = 3U;
  constexpr std::size_t IMAGE_CHANNELS = 1U;
  constexpr std::size_t IMAGE_ELEMENT_COUNT =
    IMAGE_HEIGHT * IMAGE_WIDTH * IMAGE_CHANNELS;
  const shape_t image_shape{{
    IMAGE_HEIGHT,
    IMAGE_WIDTH,
    IMAGE_CHANNELS}};
  const std::array<scalar_t, IMAGE_ELEMENT_COUNT> input{
    1.0F, 2.0F, 3.0F,
    4.0F, 5.0F, 6.0F,
    7.0F, 8.0F, 9.0F};
  std::array<scalar_t, IMAGE_ELEMENT_COUNT> output{};

  try {
    image_box_blur_c box_blur(image_shape);
    const accelerator::status_e status =
      box_blur.process(input, output, image_shape);
    if (status != accelerator::status_e::success) {
      std::cerr << "Image filter failed with status "
                << static_cast<common::uint32_t>(status) << '\n';
      return 1;
    }
  } catch (const std::exception & error) {
    std::cerr << "Image example initialization failed: "
              << error.what() << '\n';
    return 1;
  }

  std::cout << "Blurred image:\n";
  for (std::size_t row = 0U; row < IMAGE_HEIGHT; ++row) {
    for (std::size_t column = 0U; column < IMAGE_WIDTH; ++column) {
      std::cout << output[row * IMAGE_WIDTH + column];
      std::cout << (column + 1U == IMAGE_WIDTH ? '\n' : ' ');
    }
  }
  return 0;
}
