#pragma once

#include "accelerator/types.hpp"
#include "common/numeric_types.hpp"

#include <cstddef>
#include <span>

namespace accelerator_examples::vector_pipeline
{

using scalar_t = common::float32_t;
using shape_t = accelerator::tensor_shape_s<1U>;
using buffer_t = accelerator::buffer_c<scalar_t, 1U>;

class vector_pipeline_c final : private accelerator::accelerator_c
{
public:
  vector_pipeline_c(std::size_t element_count, scalar_t scale);
  ~vector_pipeline_c() noexcept;

  vector_pipeline_c(const vector_pipeline_c &) = delete;
  vector_pipeline_c & operator=(const vector_pipeline_c &) = delete;
  vector_pipeline_c(vector_pipeline_c &&) = delete;
  vector_pipeline_c & operator=(vector_pipeline_c &&) = delete;

  accelerator::status_e process(
    std::span<const scalar_t> input,
    std::span<scalar_t> scaled,
    scalar_t & sum) noexcept;

private:
  static constexpr std::size_t WORKSPACE_CAPACITY = 32U;

  accelerator::operation_c m_scale;
  accelerator::operation_c m_sum;
  buffer_t m_input;
  buffer_t m_scaled;
  buffer_t m_workspace;
  buffer_t m_result;
};

}  // namespace accelerator_examples::vector_pipeline
