#pragma once

#include "common/numeric_types.hpp"

namespace localization
{

/// Cartesian LiDAR sample in metres. Intensity and sensor-specific fields belong in dataset adapters.
struct point_3f_s
{
  common::float32_t x_m{0.0F};
  common::float32_t y_m{0.0F};
  common::float32_t z_m{0.0F};
};

struct translation_3d_s
{
  common::float64_t x_m{0.0};
  common::float64_t y_m{0.0};
  common::float64_t z_m{0.0};
};

/// Unit quaternion in x, y, z, w order.
struct quaternion_s
{
  common::float64_t x{0.0};
  common::float64_t y{0.0};
  common::float64_t z{0.0};
  common::float64_t w{1.0};
};

/// Rigid transform from the scan frame into the map frame.
struct pose_3d_s
{
  translation_3d_s translation{};
  quaternion_s rotation{};
};

}  // namespace localization
