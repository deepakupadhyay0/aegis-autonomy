#pragma once
#include <memory>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "autonomy_msgs/msg/presence_event.hpp"
#include <opencv2/opencv.hpp>
#include <thread>
#include <atomic>
#include "base_node/concurrent_ring_buffer.hpp"

#include "base_node/base_node.hpp"
#include "base_node/waitset.hpp"
#include "base_node/ipc/ipc_socket.hpp"
#include "base_node/ipc/shm_ring_buffer.hpp"
#include "presence_detection/visibility_control.hpp"
#include <onnxruntime_cxx_api.h>

namespace presence_detection
{
class PRESENCE_DETECTION_PUBLIC perception_node_c : public base_node::base_node_c
{
public:
  explicit perception_node_c(const std::vector<std::string> & args = {});
  ~perception_node_c() override;

  void step1_allocate_resources(const std::vector<std::string> & args) override;
  void step2_start_threads(const std::vector<std::string> & args) override;
  void step3_run_forever(const std::vector<std::string> & args) override;

private:
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr m_image_sub;
  rclcpp::Publisher<autonomy_msgs::msg::PresenceEvent>::SharedPtr m_presence_pub;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr m_debug_image_pub;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr m_depth_image_pub;
  
  cv::Ptr<cv::FaceDetectorYN> m_face_detector;

  // ONNX Runtime objects
  std::unique_ptr<Ort::Env> m_ort_env;
  std::unique_ptr<Ort::Session> m_ort_session;
  std::unique_ptr<Ort::MemoryInfo> m_memory_info;

  void ai_thread_loop();
  
  std::thread m_ai_thread;
  std::atomic<bool> m_running;
  std::unique_ptr<base_node::topic::concurrent_ring_buffer_c<cv::Mat>> m_frame_queue;

  std::unique_ptr<base_node::ipc::shm_ring_buffer_c> m_shm_buf;
  base_node::ipc::ipc_socket_c m_client_sock;
  bool m_using_ipc{false};
};

}  // namespace presence_detection
