#pragma once

#include "base_node/base_node.hpp"

#include <rclcpp/rclcpp.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <vector>

namespace base_node
{

template<typename node_t>
template<typename derived_node_t>
int32_t base_node_c<node_t>::create_and_execute_class(
  const int32_t argc,
  char const * const * const argv,
  const base_node_options_s & options)
{
  rclcpp::init(argc, argv);

  std::vector<std::string> args;
  args.reserve(static_cast<std::size_t>(argc));
  for (int32_t index = 0; index < argc; ++index) {
    if (argv[index] != nullptr) {
      args.emplace_back(argv[index]);
    }
  }

  try {
    const std::shared_ptr<derived_node_t> node =
      std::make_shared<derived_node_t>(args, options);
    node->execute_base_node(args);
  } catch (const std::exception & exception) {
    RCLCPP_FATAL(
      rclcpp::get_logger(options.node_name.c_str()),
      "Critical exception caught: %s",
      exception.what());
    rclcpp::shutdown();
    return -1;
  } catch (...) {
    RCLCPP_FATAL(
      rclcpp::get_logger(options.node_name.c_str()),
      "Unknown critical exception caught");
    rclcpp::shutdown();
    return -1;
  }

  rclcpp::shutdown();
  return 0;
}

}  // namespace base_node
