#pragma once

#include <memory>
#include <vector>
#include <string>
#include <exception>
#include <cstdint>

#include "rclcpp/rclcpp.hpp"
#include "base_node/visibility_control.hpp"

namespace base_node
{

BASE_NODE_PUBLIC const char * get_node_name() noexcept;

class BASE_NODE_PUBLIC base_node_c : public rclcpp::Node
{
public:
  /// @brief Constructor
  explicit base_node_c(
    const rclcpp::NodeOptions & options = rclcpp::NodeOptions())
  : rclcpp::Node(get_node_name(), options)
  {
  }

  // Delete copy/move constructors and assignment operators to ensure MISRA compliance 
  // (Prevents accidental slicing or copying of Node resources)
  base_node_c(const base_node_c &) = delete;
  base_node_c & operator=(const base_node_c &) = delete;
  base_node_c(base_node_c &&) = delete;
  base_node_c & operator=(base_node_c &&) = delete;

  virtual ~base_node_c() = default;


  virtual void step1_allocate_resources(const std::vector<std::string> & args) = 0;

  virtual void step2_start_threads(const std::vector<std::string> & args) = 0;

  virtual void step3_run_forever(const std::vector<std::string> & args) = 0;

  template<typename DerivedNodeT>
  static int32_t create_and_execute_class(int32_t argc, char const * const * argv)
  {
    rclcpp::init(argc, argv);
    
    std::vector<std::string> args;
    args.reserve(static_cast<size_t>(argc));
    for (int32_t i = 0; i < argc; ++i) {
      if (argv[i] != nullptr) {
        args.emplace_back(argv[i]);
      }
    }

    try {
      std::shared_ptr<DerivedNodeT> node = std::make_shared<DerivedNodeT>(args);
      node->step1_allocate_resources(args);
      node->step2_start_threads(args);
      node->step3_run_forever(args);
    } catch (const std::exception & e) {
      RCLCPP_FATAL(rclcpp::get_logger(get_node_name()), "Critical exception caught: %s", e.what());
      rclcpp::shutdown();
      return -1;
    } catch (...) {
      RCLCPP_FATAL(rclcpp::get_logger(get_node_name()), "Unknown critical exception caught");
      rclcpp::shutdown();
      return -1;
    }

    rclcpp::shutdown();
    return 0;
  }
};

}  // namespace base_node
