#pragma once

#include "common/numeric_types.hpp"

namespace localization
{

enum class status_e : common::uint8_t
{
  success = 0U,
  invalid_config,
  invalid_point_cloud,
  capacity_exceeded,
  insufficient_map_structure,
  map_not_ready,
  invalid_initial_pose,
  insufficient_correspondences,
  numerical_failure,
  accelerator_failure,
  step_rejected,
  not_converged
};

}  // namespace localization
