#pragma once

#include <memory>
#include <string>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <opencv2/opencv.hpp>
#include <cv_bridge/cv_bridge.hpp>

#include "base_node/base_node.hpp"
#include "presence_detection/visibility_control.hpp"

namespace presence_detection
{

class PRESENCE_DETECTION_PUBLIC camera_node_c : public base_node::base_node_c
{
public:
  explicit camera_node_c(const std::vector<std::string> & args);
  virtual ~camera_node_c() = default;

  void step1_allocate_resources(const std::vector<std::string> & args) override;
  void step2_start_threads(const std::vector<std::string> & args) override;
  void step3_run_forever(const std::vector<std::string> & args) override;

private:
  cv::VideoCapture m_cap;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr m_pub;
};

}  // namespace presence_detection
