#pragma once

#include "base_core/base_node.hpp"
#include "logging/log_macros.hpp"

#include <rclcpp/rclcpp.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace base_core
{

template<typename derived_node_t>
int32_t create_and_execute_node(
  const int32_t argc,
  char const * const * const argv,
  const base_node_options_s & options)
{
  static_assert(
    std::is_base_of_v<ros_base_node_c, derived_node_t> ||
    std::is_base_of_v<lifecycle_base_node_c, derived_node_t>,
    "create_and_execute_node requires a supported base_node_c specialization");

  rclcpp::init(argc, argv);

  std::vector<std::string> args;
  args.reserve(static_cast<std::size_t>(argc));
  for (int32_t index = 0; index < argc; ++index) {
    if (argv[index] != nullptr) {
      args.emplace_back(argv[index]);
    }
  }

  std::shared_ptr<derived_node_t> node;
  try {
    node = std::make_shared<derived_node_t>(args, options);
    node->execute_base_node(args);
  } catch (const std::exception & exception) {
    if (logging::logger_c::is_initialized()) {
      CORE_LOG_FATAL(
        "Critical exception caught: %s",
        exception.what());
    } else {
      RCLCPP_FATAL(
        rclcpp::get_logger(options.node_name.c_str()),
        "Critical exception caught before logger initialization: %s",
        exception.what());
    }
    node.reset();
    rclcpp::shutdown();
    return -1;
  } catch (...) {
    if (logging::logger_c::is_initialized()) {
      CORE_LOG_FATAL("Unknown critical exception caught");
    } else {
      RCLCPP_FATAL(
        rclcpp::get_logger(options.node_name.c_str()),
        "Unknown critical exception caught before logger initialization");
    }
    node.reset();
    rclcpp::shutdown();
    return -1;
  }

  node.reset();
  rclcpp::shutdown();
  return 0;
}

}  // namespace base_core
