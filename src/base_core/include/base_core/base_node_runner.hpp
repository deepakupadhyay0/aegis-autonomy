#pragma once

#include "base_core/base_node.hpp"

#include <rclcpp/rclcpp.hpp>

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <exception>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
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
    std::is_base_of_v<ros_base_node_c, derived_node_t>||
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
    if (options.logging != nullptr && options.logging->is_initialized()) {
      std::array<char, 256U> message{};
      const int32_t message_size = static_cast<int32_t>(std::snprintf(
          message.data(),
          message.size(),
          "Critical exception caught: %s",
          exception.what()));
      if (message_size > 0) {
        options.logging->write(
          observability::log_level_e::fatal,
          std::string_view(
            message.data(),
            static_cast<std::size_t>(message_size) < message.size() ?
            static_cast<std::size_t>(message_size) : message.size() - 1U),
          std::source_location::current());
      }
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
    if (options.logging != nullptr && options.logging->is_initialized()) {
      options.logging->write(
        observability::log_level_e::fatal,
        "Unknown critical exception caught",
        std::source_location::current());
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
