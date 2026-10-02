#pragma once

#include "autonomy_config/place_recognition.hpp"

#include <rclcpp/node.hpp>

namespace place_recognition_ros
{

autonomy_config::PlaceRecognition load_place_recognition_config(
  rclcpp::Node & node);

}  // namespace place_recognition_ros
