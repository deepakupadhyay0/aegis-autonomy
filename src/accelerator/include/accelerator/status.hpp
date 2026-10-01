#pragma once

#include "common/numeric_types.hpp"

namespace accelerator
{

enum class status_e : common::uint8_t
{
  success = 0U,
  invalid_argument,
  shape_mismatch,
  non_contiguous_host_view,
  non_contiguous_device_view,
  device_mismatch,
  overlapping_buffers,
  dimension_out_of_range,
  backend_error
};

}  // namespace accelerator
