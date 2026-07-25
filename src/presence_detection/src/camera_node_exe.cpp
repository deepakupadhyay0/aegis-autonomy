#include "presence_detection/camera_node.hpp"
#include "base_node/base_node.hpp"

namespace base_node
{
const char * get_node_name() noexcept
{
  return "camera_node";
}
}  // namespace base_node

int32_t main(int32_t argc, char ** argv)
{
  return base_node::base_node_c::create_and_execute_class<presence_detection::camera_node_c>(argc, argv);
}
