#include "presence_detection/perception_node.hpp"
#include "base_core/base_node_runner.hpp"

int32_t main(int32_t argc, char ** argv)
{
  base_core::base_node_options_s options;
  options.node_name = "perception_node";
  return base_core::create_and_execute_node<
    presence_detection::perception_node_c>(argc, argv, options);
}
