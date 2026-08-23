#pragma once

#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <atomic>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <opencv2/opencv.hpp>
#include <cv_bridge/cv_bridge.hpp>

#include "base_core/base_node.hpp"
#include "base_core/ipc/ipc_socket.hpp"
#include "base_core/ipc/shm_ring_buffer.hpp"
#include "common/ipc/ipc_protocol.hpp"
#include "presence_detection/visibility_control.hpp"

namespace presence_detection
{

class PRESENCE_DETECTION_PUBLIC camera_node_c
  : public base_core::ros_base_node_c
{
public:
  camera_node_c(
    const std::vector<std::string> & args,
    const base_core::base_node_options_s & options);
  ~camera_node_c() override;

protected:
  void step1_allocate_resources(const std::vector<std::string> & args) override;
  void step2_start_threads(const std::vector<std::string> & args) override;
  void step3_run_forever(const std::vector<std::string> & args) override;

private:
  void accept_thread_loop();

  cv::VideoCapture m_cap;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr m_pub;

  std::unique_ptr<base_core::ipc::shm_ring_buffer_c> m_shm_buf;
  common::ipc::stream_descriptor_s m_stream_descriptor;
  base_core::ipc::ipc_socket_c m_server_sock;
  base_core::ipc::ipc_socket_c m_client_sock;
  std::thread m_accept_thread;
  std::atomic<bool> m_client_connected{false};
  std::atomic<bool> m_running{false};
};

}  // namespace presence_detection
