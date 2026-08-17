#include "base_core/ros_time_conversion.hpp"

#include <rcl/time.h>

namespace base_core
{

common::time::ros_time_c from_ros_time(const rclcpp::Time & ros_time) noexcept
{
  if (ros_time.get_clock_type() != RCL_ROS_TIME) {
    return common::time::ros_time_c::invalid();
  }
  return common::time::ros_time_c::from_nanoseconds(ros_time.nanoseconds());
}

std::optional<rclcpp::Time> to_ros_time(
  const common::time::ros_time_c time) noexcept
{
  if (!time.is_valid()) {
    return std::nullopt;
  }

  try {
    return rclcpp::Time(time.nanoseconds(), RCL_ROS_TIME);
  } catch (...) {
    return std::nullopt;
  }
}

}  // namespace base_core
