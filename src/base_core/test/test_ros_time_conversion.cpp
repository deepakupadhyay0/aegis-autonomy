#include "base_core/ros_time_conversion.hpp"

#include <gtest/gtest.h>

namespace
{

TEST(RosTimeConversionTest, ConvertsRosClockWithoutChangingTicks)
{
  const rclcpp::Time source(123456789LL, RCL_ROS_TIME);
  const common::time::ros_time_c converted = base_core::from_ros_time(source);

  ASSERT_TRUE(converted.is_valid());
  EXPECT_EQ(converted.nanoseconds(), source.nanoseconds());

  const std::optional<rclcpp::Time> restored = base_core::to_ros_time(converted);
  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(restored->nanoseconds(), source.nanoseconds());
  EXPECT_EQ(restored->get_clock_type(), RCL_ROS_TIME);
}

TEST(RosTimeConversionTest, RejectsWrongClockAndInvalidTime)
{
  const rclcpp::Time system_time(123LL, RCL_SYSTEM_TIME);
  EXPECT_FALSE(base_core::from_ros_time(system_time).is_valid());
  EXPECT_FALSE(
    base_core::to_ros_time(common::time::ros_time_c::invalid()).has_value());
}

}  // namespace
