#pragma once

#include "accelerator/types.hpp"
#include "common/numeric_types.hpp"

namespace accelerator_test
{

using pattern_scalar_t = common::float32_t;
using pattern_buffer_t = accelerator::buffer_c<pattern_scalar_t, 1U>;

class cuda_pattern_test_accelerator_c final : private accelerator::accelerator_c
{
public:
  cuda_pattern_test_accelerator_c();
  ~cuda_pattern_test_accelerator_c() noexcept;

  cuda_pattern_test_accelerator_c(
    const cuda_pattern_test_accelerator_c &) = delete;
  cuda_pattern_test_accelerator_c & operator=(
    const cuda_pattern_test_accelerator_c &) = delete;
  cuda_pattern_test_accelerator_c(
    cuda_pattern_test_accelerator_c &&) = delete;
  cuda_pattern_test_accelerator_c & operator=(
    cuda_pattern_test_accelerator_c &&) = delete;

  using accelerator_c::download;
  using accelerator_c::make_buffer;
  using accelerator_c::upload;

  accelerator::status_e increment(
    const pattern_buffer_t & input,
    pattern_buffer_t & output) noexcept;

  accelerator::status_e increment_read_only_output(
    const pattern_buffer_t & input,
    const pattern_buffer_t & output) noexcept;

  accelerator::status_e sum(
    const pattern_buffer_t & input,
    pattern_buffer_t & workspace,
    pattern_buffer_t & result) noexcept;

private:
  accelerator::operation_c m_increment;
  accelerator::operation_c m_sum;
};

}  // namespace accelerator_test
