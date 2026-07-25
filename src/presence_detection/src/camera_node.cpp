#include "presence_detection/camera_node.hpp"

namespace presence_detection
{

camera_node_c::camera_node_c(const std::vector<std::string> & args)
: base_node::base_node_c()
{
  (void)args; // Unused for now
}

void camera_node_c::step1_allocate_resources(const std::vector<std::string> & args)
{
  (void)args;

  // Initialize Publisher
  m_pub = this->create_publisher<sensor_msgs::msg::Image>("camera/image_raw", 10);

  // Initialize OpenCV VideoCapture (Defaulting to /dev/video0)
  m_cap.open(0);
  if (!m_cap.isOpened()) {
    throw std::runtime_error("Failed to open OpenCV VideoCapture on device 0");
  }

  // Force strict zero-buffering for low latency determinism
  m_cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
  
  RCLCPP_INFO(this->get_logger(), "step1_allocate_resources complete: Camera and Publisher ready.");
}

void camera_node_c::step2_start_threads(const std::vector<std::string> & args)
{
  (void)args;
  // Intentionally empty. No background executors needed for a strict publisher.
  RCLCPP_INFO(this->get_logger(), "step2_start_threads complete: No executors spawned.");
}

void camera_node_c::step3_run_forever(const std::vector<std::string> & args)
{
  (void)args;
  RCLCPP_INFO(this->get_logger(), "step3_run_forever: Entering main acquisition loop.");

  cv::Mat frame;
  while (rclcpp::ok()) {
    m_cap >> frame;
    
    if (frame.empty()) {
      RCLCPP_WARN(this->get_logger(), "Captured empty frame, skipping.");
      continue;
    }

    // Convert cv::Mat to ROS 2 message
    std_msgs::msg::Header header;
    header.stamp = this->now();
    header.frame_id = "camera_link";
    sensor_msgs::msg::Image::SharedPtr msg = cv_bridge::CvImage(header, "bgr8", frame).toImageMsg();
    m_pub->publish(*msg);
  }
}

}  // namespace presence_detection
