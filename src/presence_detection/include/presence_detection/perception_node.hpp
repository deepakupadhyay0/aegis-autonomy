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
#include "base_core/concurrent_ring_buffer.hpp"

#include "base_core/base_node.hpp"
#include "base_core/execution/waitset.hpp"
#include "base_core/ipc/ipc_socket.hpp"
#include "base_core/ipc/shm_ring_buffer.hpp"
#include "common/ipc/ipc_protocol.hpp"
#include "presence_detection/visibility_control.hpp"
#include <onnxruntime_cxx_api.h>

namespace presence_detection
{
class PRESENCE_DETECTION_PUBLIC perception_node_c :
  public base_core::ros_base_node_c
{
public:
  perception_node_c(
    const std::vector<std::string> & args,
    const base_core::base_node_options_s & options);
  ~perception_node_c() override;

protected:
  void step1_allocate_resources(const std::vector<std::string> & args) override;
  void step2_start_threads(const std::vector<std::string> & args) override;
  void step3_run_forever(const std::vector<std::string> & args) override;

private:
  rclcpp::CallbackGroup::SharedPtr m_waitset_callback_group;
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
  std::unique_ptr<base_core::topic::concurrent_ring_buffer_c<cv::Mat>> m_frame_queue;

  std::unique_ptr<base_core::ipc::shm_ring_buffer_c> m_shm_buf;
  common::ipc::stream_descriptor_s m_stream_descriptor;
  base_core::ipc::ipc_socket_c m_client_sock;
  bool m_using_ipc{false};
  uint64_t m_last_sequence{0U};
};

}  // namespace presence_detection
