#pragma once

#include "base_core/visibility_control.hpp"
#include "common/time.hpp"

#include <rclcpp/time.hpp>

#include <optional>

namespace base_core
{

BASE_CORE_PUBLIC common::time::ros_time_c from_ros_time(
  const rclcpp::Time & ros_time) noexcept;

BASE_CORE_PUBLIC std::optional<rclcpp::Time> to_ros_time(
  const common::time::ros_time_c time) noexcept;

}  // namespace base_core
