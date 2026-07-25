#include "presence_detection/perception_node.hpp"

namespace base_node {
const char * get_node_name() noexcept {
  return "perception_node";
}
}

int main(int argc, char * argv[])
{
  return base_node::base_node_c::create_and_execute_class<
    presence_detection::perception_node_c>(argc, argv);
}
