#include "presence_detection/camera_node.hpp"
#include "base_node/base_node_runner.hpp"

int32_t main(int32_t argc, char ** argv)
{
  base_node::base_node_options_s options;
  options.node_name = "camera_node";
  return base_node::ros_base_node_c::create_and_execute_class<
    presence_detection::camera_node_c>(argc, argv, options);
}
