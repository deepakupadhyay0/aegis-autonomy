#pragma once

#include "autonomy_config/localization.hpp"

#include <rclcpp/node.hpp>

namespace localization_ros
{

autonomy_config::Localization load_localization_config(rclcpp::Node & node);

}  // namespace localization_ros
