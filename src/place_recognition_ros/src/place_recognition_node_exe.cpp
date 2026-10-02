#include "place_recognition_ros/place_recognition_node.hpp"

#include "base_core/base_node_runner.hpp"
#include "logging/node_logging.hpp"

#include <memory>

common::int32_t main(common::int32_t argc, char ** argv)
{
  base_core::base_node_options_s options;
  options.node_name = "place_recognition_node";
  options.logging = std::make_shared<logging::node_logging_adapter_c>();
  return base_core::create_and_execute_node<
    place_recognition_ros::place_recognition_node_c>(argc, argv, options);
}
