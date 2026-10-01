#pragma once

#include "accelerator/types.hpp"
#include "common/numeric_types.hpp"

#include <span>

namespace accelerator_examples::image_box_blur
{

using scalar_t = common::float32_t;
using shape_t = accelerator::tensor_shape_s<3U>;
using buffer_t = accelerator::buffer_c<scalar_t, 3U>;

class image_box_blur_c final : private accelerator::accelerator_c
{
public:
  explicit image_box_blur_c(const shape_t & image_shape);
  ~image_box_blur_c() noexcept;

  image_box_blur_c(const image_box_blur_c &) = delete;
  image_box_blur_c & operator=(const image_box_blur_c &) = delete;
  image_box_blur_c(image_box_blur_c &&) = delete;
  image_box_blur_c & operator=(image_box_blur_c &&) = delete;

  accelerator::status_e process(
    std::span<const scalar_t> input,
    std::span<scalar_t> output,
    const shape_t & image_shape) noexcept;

private:
  accelerator::operation_c m_box_blur;
  buffer_t m_input;
  buffer_t m_output;
};

}  // namespace accelerator_examples::image_box_blur
