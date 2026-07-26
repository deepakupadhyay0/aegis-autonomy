#include "presence_detection/camera_node.hpp"
#include <autonomy_config/autonomy_settings.hpp>
#include <cstring>

namespace presence_detection
{

camera_node_c::camera_node_c(const std::vector<std::string> & args)
: base_node::base_node_c()
{
  (void)args; // Unused for now
}

camera_node_c::~camera_node_c()
{
  m_running = false;
  m_server_sock.close_socket();
  m_client_sock.close_socket();
  if (m_accept_thread.joinable()) {
    m_accept_thread.join();
  }
}

void camera_node_c::step1_allocate_resources(const std::vector<std::string> & args)
{
  (void)args;
  auto const config = autonomy_config::AutonomySettings::get_run_time_values();
  m_pub = this->create_publisher<sensor_msgs::msg::Image>("camera/image_raw", 10);

  // Initialize OpenCV VideoCapture using TOML config
  m_cap.open(static_cast<int>(config.get_camera().get_device_id()));
  if (!m_cap.isOpened()) {
    throw std::runtime_error("Failed to open OpenCV VideoCapture on configured device ID");
  }
  m_cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
  m_cap.set(cv::CAP_PROP_FRAME_WIDTH, config.get_camera().get_image_width());
  m_cap.set(cv::CAP_PROP_FRAME_HEIGHT, config.get_camera().get_image_height());
  m_cap.set(cv::CAP_PROP_FPS, config.get_camera().get_fps());

  // Initialize SHM ring buffer using TOML resolution
  const uint32_t num_slots = 3U;
  const size_t slot_size = static_cast<size_t>(config.get_camera().get_image_width()) * static_cast<size_t>(config.get_camera().get_image_height()) * 3U;
  m_shm_buf = std::make_unique<base_node::ipc::shm_ring_buffer_c>(num_slots, slot_size);
  if (m_shm_buf->create_anonymous_shm() != core_ret_e::ok) {
    RCLCPP_WARN(this->get_logger(), "Failed to create anonymous SHM buffer for IPC.");
  } else {
    if (m_server_sock.bind_and_listen("@presence_detection_ipc") == core_ret_e::ok) {
      RCLCPP_INFO(this->get_logger(), "IPC Server listening on abstract socket @presence_detection_ipc");
    } else {
      RCLCPP_WARN(this->get_logger(), "Failed to bind IPC socket on @presence_detection_ipc");
    }
  }

  m_running = true;
  RCLCPP_INFO(this->get_logger(), "step1_allocate_resources complete: Camera and IPC ready.");
}

void camera_node_c::accept_thread_loop()
{
  RCLCPP_INFO(this->get_logger(), "IPC Acceptor thread waiting for client connection...");
  if (m_server_sock.accept_client(m_client_sock) == core_ret_e::ok) {
    RCLCPP_INFO(this->get_logger(), "IPC Client connected! Sending SHM file descriptor via SCM_RIGHTS...");
    base_node::ipc::ipc_message_t handshake = {};
    handshake.msg_id = 0; // Handshake
    if (m_client_sock.send_message(handshake, m_shm_buf->get_shm_fd()) == core_ret_e::ok) {
      m_client_connected = true;
      RCLCPP_INFO(this->get_logger(), "SHM file descriptor successfully transmitted to client!");
    } else {
      RCLCPP_ERROR(this->get_logger(), "Failed to send SHM fd handshake to client.");
    }
  }
}

void camera_node_c::step2_start_threads(const std::vector<std::string> & args)
{
  (void)args;
  if (m_shm_buf && m_shm_buf->is_valid() && m_server_sock.is_valid()) {
    m_accept_thread = std::thread(&camera_node_c::accept_thread_loop, this);
  }
  RCLCPP_INFO(this->get_logger(), "step2_start_threads complete.");
}

void camera_node_c::step3_run_forever(const std::vector<std::string> & args)
{
  (void)args;
  RCLCPP_INFO(this->get_logger(), "step3_run_forever: Entering main acquisition loop.");

  cv::Mat frame;
  uint32_t slot_idx = 0U;
  while (rclcpp::ok() && m_running.load()) {
    m_cap >> frame;
    
    if (frame.empty()) {
      RCLCPP_WARN(this->get_logger(), "Captured empty frame, skipping.");
      continue;
    }

    // 1. Send over IPC if client is connected and buffer is large enough
    if (m_client_connected.load() && m_shm_buf && m_shm_buf->is_valid()) {
      const size_t frame_bytes = static_cast<size_t>(frame.total()) * frame.elemSize();
      if (frame_bytes <= m_shm_buf->get_slot_size()) {
        void * slot_ptr = m_shm_buf->get_slot_pointer(slot_idx);
        if (slot_ptr != nullptr) {
          std::memcpy(slot_ptr, frame.data, frame_bytes);
          base_node::ipc::ipc_message_t msg = {};
          msg.msg_id = 1;
          msg.slot_index = slot_idx;
          msg.width = static_cast<uint32_t>(frame.cols);
          msg.height = static_cast<uint32_t>(frame.rows);
          msg.format = 1; // BGR8
          msg.timestamp_ns = static_cast<uint64_t>(this->now().nanoseconds());
          
          m_client_sock.send_message(msg);
          slot_idx = (slot_idx + 1) % m_shm_buf->get_num_slots();
        }
      }
    }

    // 2. Also publish over DDS for debug tools / RViz
    std_msgs::msg::Header header;
    header.stamp = this->now();
    header.frame_id = "camera_link";
    sensor_msgs::msg::Image::SharedPtr msg = cv_bridge::CvImage(header, "bgr8", frame).toImageMsg();
    m_pub->publish(*msg);
  }
}

}  // namespace presence_detection
