#include "presence_detection/camera_node.hpp"
#include "presence_detection/ipc/camera_ipc_validation.hpp"
#include "base_core/execution/thread_name.hpp"
#include "common/ipc/ipc_codec.hpp"
#include "common/resource_names.hpp"
#include "logging/log_macros.hpp"
#include <autonomy_config/autonomy_settings.hpp>
#include <array>
#include <cinttypes>
#include <limits>
#include <span>

namespace presence_detection
{

camera_node_c::camera_node_c(
  const std::vector<std::string> & args,
  const base_core::base_node_options_s & options)
: base_core::ros_base_node_c(options)
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
  static_cast<void>(m_cap.set(cv::CAP_PROP_BUFFERSIZE, 1));
  const bool8_t width_requested =
    m_cap.set(
    cv::CAP_PROP_FRAME_WIDTH,
    config.get_camera().get_image_width());
  const bool8_t height_requested =
    m_cap.set(
    cv::CAP_PROP_FRAME_HEIGHT,
    config.get_camera().get_image_height());
  const bool8_t fps_requested =
    m_cap.set(cv::CAP_PROP_FPS, config.get_camera().get_fps());
  if (!width_requested || !height_requested || !fps_requested) {
    CORE_LOG_WARN(
      "Camera driver did not accept every requested capture property.");
  }

  cv::Mat initial_frame;
  if (!m_cap.read(initial_frame) || initial_frame.empty()) {
    throw std::runtime_error(
      "Failed to capture initial frame from OpenCV VideoCapture");
  }

  const common::uint32_t num_slots = 3U;
  common::ipc::stream_descriptor_s stream_descriptor;
  if (!presence_detection::ipc::make_camera_stream_descriptor(
      initial_frame,
      num_slots,
      stream_descriptor))
  {
    throw std::runtime_error(
      "OpenCV VideoCapture produced an unsupported initial frame");
  }
  if (stream_descriptor.slot_size >
    std::numeric_limits<std::size_t>::max())
  {
    throw std::runtime_error(
      "OpenCV VideoCapture frame is too large for shared memory");
  }

  const common::int64_t requested_width =
    config.get_camera().get_image_width();
  const common::int64_t requested_height =
    config.get_camera().get_image_height();
  if (requested_width != initial_frame.cols ||
    requested_height != initial_frame.rows)
  {
    CORE_LOG_WARN(
      "Camera negotiated %dx%d instead of requested %" PRId64
      "x%" PRId64 ".",
      initial_frame.cols,
      initial_frame.rows,
      requested_width,
      requested_height);
  }

  const std::size_t slot_size =
    static_cast<std::size_t>(stream_descriptor.slot_size);
  m_stream_descriptor = stream_descriptor;
  std::array<std::byte, common::ipc::STREAM_DESCRIPTOR_SIZE> descriptor_buffer;
  common::ipc::serialize_stream_descriptor(m_stream_descriptor, descriptor_buffer);

  m_shm_buf = std::make_unique<base_core::ipc::shm_ring_buffer_c>(
    num_slots,
    slot_size);
  if (m_shm_buf->create_anonymous_shm(
      aegis_autonomy::resources::CAMERA_FRONT,
      descriptor_buffer) !=
    core_ret_e::ok)
  {
    CORE_LOG_WARN(
      "Failed to create anonymous SHM buffer for IPC.");
  } else {
    if (m_server_sock.bind_and_listen("@presence_detection_ipc") == core_ret_e::ok) {
      CORE_LOG_INFO(
        "IPC server listening on abstract socket @presence_detection_ipc");
    } else {
      CORE_LOG_WARN(
        "Failed to bind IPC socket on @presence_detection_ipc");
    }
  }

  m_running = true;
  CORE_LOG_INFO(
    "step1_allocate_resources complete: camera and IPC ready");
}

void camera_node_c::accept_thread_loop()
{
  base_core::execution::set_current_thread_name("ipc_acceptor");
  CORE_LOG_INFO(
    "IPC acceptor thread waiting for a client connection");
  if (m_server_sock.accept_client(m_client_sock) == core_ret_e::ok) {
    CORE_LOG_INFO(
      "IPC client connected; sending SHM descriptor with SCM_RIGHTS");
    if (m_client_sock.send_handshake(m_shm_buf->get_shm_fd()) == core_ret_e::ok) {
      m_client_connected = true;
      CORE_LOG_INFO(
        "SHM file descriptor transmitted to IPC client");
    } else {
      CORE_LOG_ERROR(
        "Failed to send SHM descriptor handshake to IPC client");
    }
  }
}

void camera_node_c::step2_start_threads(const std::vector<std::string> & args)
{
  (void)args;
  if (m_shm_buf && m_shm_buf->is_valid() && m_server_sock.is_valid()) {
    m_accept_thread = std::thread(&camera_node_c::accept_thread_loop, this);
  }
  CORE_LOG_INFO("step2_start_threads complete");
}

void camera_node_c::step3_run_forever(const std::vector<std::string> & args)
{
  (void)args;
  CORE_LOG_INFO(
    "step3_run_forever: entering main acquisition loop");

  cv::Mat frame;
  uint32_t slot_idx = 0U;
  uint64_t sequence = 0U;
  while (this->ok() && m_running.load()) {
    m_cap >> frame;
    
    if (frame.empty()) {
      CORE_LOG_WARN("Captured empty frame; skipping");
      continue;
    }

    // 1. Send over IPC if client is connected and buffer is large enough
    if (m_client_connected.load() && m_shm_buf && m_shm_buf->is_valid()) {
      const common::ipc::stream_descriptor_s & descriptor = m_stream_descriptor;
      if (!presence_detection::ipc::is_camera_frame_compatible(
          frame,
          descriptor))
      {
        CORE_LOG_ERROR_THROTTLE(
          std::chrono::seconds(5),
          "Captured frame does not match the immutable IPC stream descriptor");
      } else {
        const common::uint64_t next_sequence = sequence + 1U;
        common::uint32_t claimed_slot = slot_idx;
        if (m_shm_buf->try_acquire_slot_for_write(
            next_sequence,
            claimed_slot))
        {
          void * const slot_ptr = m_shm_buf->get_slot_pointer(claimed_slot);
          const bool8_t frame_copied =
            slot_ptr != nullptr &&
            presence_detection::ipc::copy_camera_frame_to_buffer(
            frame,
            descriptor,
            std::span<std::byte>(
              static_cast<std::byte *>(slot_ptr),
              m_shm_buf->get_slot_size()));
          if (!frame_copied) {
            throw std::runtime_error(
              "Failed to copy validated camera frame into shared memory");
          }

          if (m_shm_buf->publish_written_slot(
              claimed_slot,
              next_sequence) != core_ret_e::ok)
          {
            throw std::runtime_error(
              "Failed to publish a validated shared-memory slot");
          }

          sequence = next_sequence;
          common::ipc::frame_notification_s notification;
          notification.sequence = sequence;
          notification.slot_index = claimed_slot;
          notification.timestamp_ns =
            static_cast<uint64_t>(this->now().nanoseconds());

          if (m_client_sock.send_frame_notification(notification) ==
            core_ret_e::ok)
          {
            slot_idx = (claimed_slot + 1U) % m_shm_buf->get_num_slots();
          } else {
            m_client_connected = false;
          }
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
