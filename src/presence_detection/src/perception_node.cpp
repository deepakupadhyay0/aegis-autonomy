#include "presence_detection/perception_node.hpp"
#include "presence_detection/ipc/camera_ipc_validation.hpp"
#include "base_core/execution/thread_name.hpp"
#include "common/ipc/ipc_codec.hpp"
#include "logging/log_macros.hpp"
#include <autonomy_config/autonomy_settings.hpp>
#include <cv_bridge/cv_bridge.hpp>
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <iostream>
#include <numeric>
#include <cmath>
#include <cinttypes>
#include <cstddef>
#include <span>
#include <utility>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>

namespace presence_detection
{

perception_node_c::perception_node_c(
  const std::vector<std::string> & args,
  const base_core::base_node_options_s & options)
: base_core::ros_base_node_c(options)
{
  (void)args;
}

perception_node_c::~perception_node_c()
{
  m_running = false;
  m_client_sock.close_socket();
  if (m_frame_queue) m_frame_queue->shutdown();
  if (m_ai_thread.joinable()) {
    m_ai_thread.join();
  }
}

void perception_node_c::step1_allocate_resources(const std::vector<std::string> & args)
{
  (void)args;
  
  m_presence_pub = this->create_publisher<autonomy_msgs::msg::PresenceEvent>("presence_detected", 10);
  m_debug_image_pub = this->create_publisher<sensor_msgs::msg::Image>("camera/image_debug", 10);
  m_depth_image_pub = this->create_publisher<sensor_msgs::msg::Image>("camera/depth_debug", 10);
  
  m_waitset_callback_group = this->create_callback_group(
    rclcpp::CallbackGroupType::MutuallyExclusive,
    false);
  rclcpp::SubscriptionOptions subscription_options;
  subscription_options.callback_group = m_waitset_callback_group;
  m_image_sub = this->create_subscription<sensor_msgs::msg::Image>(
    "camera/image_raw",
    10,
    [](const sensor_msgs::msg::Image::SharedPtr) {},
    subscription_options);

  auto const config = autonomy_config::AutonomySettings::get_run_time_values();
  std::string pkg_share_dir = ament_index_cpp::get_package_share_directory("presence_detection");
  std::string yunet_model_path = pkg_share_dir + "/models/face_detection_yunet.onnx";
  try {
    m_face_detector = cv::FaceDetectorYN::create(
      yunet_model_path, "", cv::Size(320, 320),
      static_cast<float32_t>(
        config.get_perception().get_confidence_threshold()),
      static_cast<float32_t>(
        config.get_perception().get_nms_threshold()),
      5000);
    CORE_LOG_INFO(
      "YuNet face detector loaded with TOML configuration");
  } catch (const std::exception& e) {
    CORE_LOG_ERROR(
      "Failed to load YuNet: %s",
      e.what());
  }

  m_frame_queue = std::make_unique<base_core::topic::concurrent_ring_buffer_c<cv::Mat>>(2);

  if (m_client_sock.connect_to_server("@presence_detection_ipc") == core_ret_e::ok) {
    CORE_LOG_INFO(
      "Connected to IPC server; waiting for SHM descriptor handshake");
    int32_t shm_fd = -1;
    if (m_client_sock.receive_handshake(shm_fd) == core_ret_e::ok) {
      m_shm_buf = std::make_unique<base_core::ipc::shm_ring_buffer_c>();
      common::ipc::stream_descriptor_s stream_descriptor;
      const bool8_t ring_attached =
        m_shm_buf->attach_from_fd(shm_fd) == core_ret_e::ok;
      const std::span<const std::byte> metadata =
        ring_attached ? m_shm_buf->get_metadata() : std::span<const std::byte>{};
      const bool8_t descriptor_decoded =
        common::ipc::deserialize_stream_descriptor(
          metadata.data(),
          metadata.size(),
          stream_descriptor);
      if (ring_attached &&
        descriptor_decoded &&
        stream_descriptor.num_slots == m_shm_buf->get_num_slots() &&
        stream_descriptor.slot_size == m_shm_buf->get_slot_size() &&
        presence_detection::ipc::validate_camera_stream_descriptor(stream_descriptor))
      {
        m_stream_descriptor = stream_descriptor;
        m_using_ipc = true;
        CORE_LOG_INFO(
          "Attached to SHM ring buffer over IPC");
      } else {
        m_shm_buf.reset();
      }
    }
  }

  if (!m_using_ipc) {
    CORE_LOG_WARN(
      "IPC handshake unavailable; falling back to DDS subscription");
  }

  m_running = true;

  // Initialize ONNX Runtime
  m_ort_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "DepthAnythingV2");
  
  Ort::SessionOptions session_options;
  const uint32_t hw_cores = std::thread::hardware_concurrency();
  const int32_t optimal_threads = static_cast<int32_t>(std::max(1U, hw_cores / 4U));
  cv::setNumThreads(optimal_threads);
  session_options.SetIntraOpNumThreads(optimal_threads);
  CORE_LOG_INFO(
    "OpenCV, YuNet, and ONNX Runtime configured with %" PRId32
    " CPU threads from %" PRIu32 " hardware threads",
    optimal_threads,
    hw_cores);
  session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

  // Enable CUDA if available, otherwise fallback to CPU
  try {
    OrtCUDAProviderOptions cuda_options;
    cuda_options.device_id = 0;
    session_options.AppendExecutionProvider_CUDA(cuda_options);
    CORE_LOG_INFO(
      "ONNX Runtime CUDA execution provider enabled");
  } catch (const std::exception& e) {
    CORE_LOG_WARN(
      "CUDA execution provider unavailable; using CPU: %s",
      e.what());
  }

  std::string model_path = pkg_share_dir + "/models/depth_anything_v2.onnx";
  try {
    m_ort_session = std::make_unique<Ort::Session>(*m_ort_env, model_path.c_str(), session_options);
    m_memory_info = std::make_unique<Ort::MemoryInfo>(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));
    CORE_LOG_INFO(
      "Depth Anything V2 Small model loaded");
  } catch (const std::exception& e) {
    CORE_LOG_ERROR(
      "Failed to load ONNX model: %s",
      e.what());
  }
}

void perception_node_c::step2_start_threads(const std::vector<std::string> & args)
{
  (void)args;
  m_ai_thread = std::thread(&perception_node_c::ai_thread_loop, this);
}

void perception_node_c::step3_run_forever(const std::vector<std::string> & args)
{
  (void)args;
  CORE_LOG_INFO(
    "Main thread listening for camera frames using %s",
    m_using_ipc ? "IPC" : "DDS fallback");

  base_core::execution::waitset_c<sensor_msgs::msg::Image> waitset(
    *this,
    m_image_sub);

  while (this->ok() && m_running.load()) {
    if (m_using_ipc && m_shm_buf && m_shm_buf->is_valid()) {
      common::ipc::frame_notification_s notification;
      if (m_client_sock.receive_frame_notification(notification) == core_ret_e::ok) {
        if (notification.sequence > m_last_sequence &&
          notification.slot_index < m_shm_buf->get_num_slots())
        {
          const common::ipc::stream_descriptor_s & descriptor = m_stream_descriptor;
          if (m_shm_buf->try_acquire_slot_for_read(
              notification.slot_index, notification.sequence))
          {
            void * const slot_ptr = m_shm_buf->get_slot_pointer(notification.slot_index);
            const int32_t cv_type =
              descriptor.format == common::ipc::pixel_format_e::mono8 ?
              CV_8UC1 : CV_8UC3;
            cv::Mat shm_frame(
              static_cast<int32_t>(descriptor.height),
              static_cast<int32_t>(descriptor.width),
              cv_type,
              slot_ptr,
              static_cast<size_t>(descriptor.stride));
            cv::Mat frame;
            try {
              frame = shm_frame.clone();
            } catch (...) {
              m_shm_buf->release_read_slot(
                notification.slot_index, notification.sequence);
              throw;
            }
            m_shm_buf->release_read_slot(notification.slot_index, notification.sequence);
            m_last_sequence = notification.sequence;
            m_frame_queue->push_back(std::move(frame));
          }
        }
      } else {
        CORE_LOG_WARN(
          "IPC peer disconnected; falling back to DDS");
        m_using_ipc = false;
        m_shm_buf.reset();
        m_stream_descriptor = common::ipc::stream_descriptor_s{};
        m_last_sequence = 0U;
      }
    } else {
      const rcl_ret_t wait_result =
        waitset.wait_for_message(std::chrono::milliseconds(100));
      if (wait_result == RCL_RET_OK && waitset.has_new_message<0>()) {
        const sensor_msgs::msg::Image & msg = waitset.get_message<0>();
        try {
          cv_bridge::CvImagePtr cv_ptr = cv_bridge::toCvCopy(msg, "bgr8");
          cv::Mat frame = cv_ptr->image;
          
          m_frame_queue->push_back(std::move(frame));
        } catch (cv_bridge::Exception& e) {
          CORE_LOG_ERROR(
            "cv_bridge exception: %s",
            e.what());
        }
      } else if (
        wait_result != RCL_RET_OK &&
        wait_result != RCL_RET_TIMEOUT &&
        wait_result != RCL_RET_ALREADY_SHUTDOWN)
      {
        CORE_LOG_ERROR(
          "DDS wait set failed with rcl return code %" PRId32,
          static_cast<int32_t>(wait_result));
        break;
      }
    }
  }
}

void perception_node_c::ai_thread_loop()
{
  base_core::execution::set_current_thread_name("ai_inference");
  CORE_LOG_INFO(
    "Background AI thread started");

  // Depth Anything V2 input dimensions
  const int32_t depth_input_w = 512; // Downscaled via binning
  const int32_t depth_input_h = 512;
  const size_t input_tensor_size = 1U * 3U * static_cast<size_t>(depth_input_h) * static_cast<size_t>(depth_input_w);
  std::vector<int64_t> input_node_dims = {1, 3, depth_input_h, depth_input_w};

  auto last_frame_time = std::chrono::high_resolution_clock::now();
  float64_t smoothed_fps = 0.0;
  int32_t frame_count = 0;

  while (m_running && this->ok()) {
    std::optional<cv::Mat> opt_frame = m_frame_queue->wait_and_pop_front();
    if (!opt_frame.has_value()) {
      continue;
    }
    
    try {
      const cv::Mat frame = opt_frame.value();

      auto const now = std::chrono::high_resolution_clock::now();
      const std::chrono::duration<float64_t> dt = now - last_frame_time;
      last_frame_time = now;
      
      const float64_t inst_fps = 1.0 / dt.count();
      if (frame_count < 5) {
        smoothed_fps = inst_fps;
      } else {
        smoothed_fps = (0.9 * smoothed_fps) + (0.1 * inst_fps);
      }
      frame_count++;
        cv::Mat faces;
        if (m_face_detector) {
          m_face_detector->setInputSize(frame.size());
          m_face_detector->detect(frame, faces);
        }

        const bool8_t presence = (faces.rows > 0);
        float32_t max_confidence = 0.0F;
        if (presence) {
          for (int32_t index = 0; index < faces.rows; ++index) {
            const float32_t confidence = faces.at<float32_t>(index, 14);
            if (confidence > max_confidence) {
              max_confidence = confidence;
            }
          }
        }
        auto msg_presence = std::make_unique<autonomy_msgs::msg::PresenceEvent>();
        msg_presence->header.stamp = this->now();
        msg_presence->header.frame_id = "camera_frame";
        msg_presence->presence_detected = presence;
        msg_presence->confidence = max_confidence;
        m_presence_pub->publish(std::move(msg_presence));

        // 2. Prepare ONNX Input conditionally (only if model is loaded AND presence is detected)
        cv::Mat depth_map_colored;
        cv::Mat depth_map_raw;
        if (presence && m_ort_session) {
          cv::Mat resized_img;
          cv::resize(frame, resized_img, cv::Size(depth_input_w, depth_input_h), 0, 0, cv::INTER_AREA);

          cv::Mat blob = cv::dnn::blobFromImage(
            resized_img,
            1.0 / 255.0,
            cv::Size(depth_input_w, depth_input_h),
            cv::Scalar(0.485 * 255.0, 0.456 * 255.0, 0.406 * 255.0),
            true,
            false);

          const size_t plane_size = static_cast<size_t>(depth_input_h) * static_cast<size_t>(depth_input_w);
          float32_t* const blob_ptr = blob.ptr<float32_t>();
          cv::Mat channel_r(depth_input_h, depth_input_w, CV_32F, blob_ptr);
          cv::Mat channel_g(depth_input_h, depth_input_w, CV_32F, blob_ptr + plane_size);
          cv::Mat channel_b(depth_input_h, depth_input_w, CV_32F, blob_ptr + (2 * plane_size));

          channel_r *= (1.0f / 0.229f);
          channel_g *= (1.0f / 0.224f);
          channel_b *= (1.0f / 0.225f);

          Ort::Value input_tensor = Ort::Value::CreateTensor<float32_t>(
            *m_memory_info, blob_ptr, input_tensor_size, input_node_dims.data(), input_node_dims.size());

          Ort::AllocatorWithDefaultOptions allocator;
          Ort::AllocatedStringPtr input_name_ptr = m_ort_session->GetInputNameAllocated(0, allocator);
          Ort::AllocatedStringPtr output_name_ptr = m_ort_session->GetOutputNameAllocated(0, allocator);
          const char* const input_names[] = {input_name_ptr.get()};
          const char* const output_names[] = {output_name_ptr.get()};

          std::vector<Ort::Value> output_tensors = m_ort_session->Run(
            Ort::RunOptions{nullptr}, input_names, &input_tensor, 1, output_names, 1);

          float32_t * const floatarr =
            output_tensors.front().GetTensorMutableData<float32_t>();
          depth_map_raw = cv::Mat(depth_input_h, depth_input_w, CV_32F, floatarr).clone();
          
          // Normalize for visualization
          cv::Mat depth_map_vis;
          cv::normalize(depth_map_raw, depth_map_vis, 0, 255, cv::NORM_MINMAX, CV_8U);
          cv::applyColorMap(depth_map_vis, depth_map_colored, cv::COLORMAP_INFERNO);
          cv::resize(depth_map_colored, depth_map_colored, frame.size());
        }

        // Extract Eyes & Calculate Metric Depth
        float32_t metric_depth_meters = -1.0f;
        for (int32_t i = 0; i < faces.rows; i++) {
          const float32_t* const data = faces.ptr<float32_t>(i);
          const float32_t score = data[14];
          if (score < 0.8f) continue;

          const cv::Rect face(
            static_cast<int32_t>(data[0]), static_cast<int32_t>(data[1]),
            static_cast<int32_t>(data[2]), static_cast<int32_t>(data[3]));
          cv::rectangle(frame, face, cv::Scalar(0, 255, 0), 2);
          
          const float32_t eye1_x = data[4];
          const float32_t eye1_y = data[5];
          const float32_t eye2_x = data[6];
          const float32_t eye2_y = data[7];

          const cv::Point2f right_eye(eye1_x, eye1_y);
          const cv::Point2f left_eye(eye2_x, eye2_y);

          // Visualize the metric depth anchor points
          cv::circle(frame, right_eye, 3, cv::Scalar(0, 255, 0), -1);
          cv::circle(frame, left_eye, 3, cv::Scalar(0, 255, 0), -1);
          cv::line(frame, right_eye, left_eye, cv::Scalar(0, 255, 0), 2);

          const float32_t pixel_dist = std::sqrt(std::pow(eye2_x - eye1_x, 2.0f) + std::pow(eye2_y - eye1_y, 2.0f));
            
          // Generic camera assumption: 60 deg FOV, approx focal length
          const float32_t focal_length = (frame.cols / 2.0f) / std::tan(30.0f * static_cast<float32_t>(M_PI) / 180.0f);
          const float32_t real_eye_dist = 0.063f; // 63mm

          // Absolute Metric Depth Z
          metric_depth_meters = (focal_length * real_eye_dist) / pixel_dist;

          if (m_ort_session && !depth_map_raw.empty()) {
            const float32_t face_cx = face.x + (face.width / 2.0f);
            const float32_t face_cy = face.y + (face.height / 2.0f);
            const int32_t dx = std::max(
              0, std::min(static_cast<int32_t>(face_cx * (depth_input_w / static_cast<float32_t>(frame.cols))), depth_input_w - 1));
            const int32_t dy = std::max(
              0, std::min(static_cast<int32_t>(face_cy * (depth_input_h / static_cast<float32_t>(frame.rows))), depth_input_h - 1));

            const float32_t rel_depth = depth_map_raw.at<float32_t>(dy, dx);
            const float32_t scale_factor = metric_depth_meters * rel_depth;
            
            char8_t scale_text[50];
            snprintf(reinterpret_cast<char*>(scale_text), sizeof(scale_text), "Scale: %.2f", scale_factor);
            cv::putText(
              frame, reinterpret_cast<char*>(scale_text),
              cv::Point(10, 110), cv::FONT_HERSHEY_SIMPLEX, 1,
              cv::Scalar(0, 255, 255), 2);
          }
        }
        
        // Draw Output
        std::string status_text = presence ? "Presence: Detected" : "Presence: Not Detected";
        cv::Scalar color = presence ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255);
        cv::putText(frame, status_text, cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1, color, 2);

        if (metric_depth_meters > 0) {
          char depth_text[50];
          snprintf(depth_text, sizeof(depth_text), "Depth: %.2f meters", metric_depth_meters);
          cv::putText(frame, depth_text, cv::Point(10, 70), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 255, 0), 2);
        }

        // Publish depth map on its own separate topic (Zero-Copy)
        if (!depth_map_colored.empty()) {
          auto depth_msg = std::make_unique<sensor_msgs::msg::Image>();
          cv_bridge::CvImage(std_msgs::msg::Header(), "bgr8", depth_map_colored).toImageMsg(*depth_msg);
          m_depth_image_pub->publish(std::move(depth_msg));
        }

        // Draw smoothed FPS
        char fps_text[50];
        snprintf(fps_text, sizeof(fps_text), "FPS: %.1f", smoothed_fps);
        cv::putText(frame, fps_text, cv::Point(frame.cols - 160, 30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);

        // Publish main debug image (Zero-Copy)
        auto debug_msg = std::make_unique<sensor_msgs::msg::Image>();
        cv_bridge::CvImage(std_msgs::msg::Header(), "bgr8", frame).toImageMsg(*debug_msg);
        m_debug_image_pub->publish(std::move(debug_msg));

      } catch (const cv_bridge::Exception& e) {
        CORE_LOG_ERROR(
          "cv_bridge exception: %s; initiating shutdown",
          e.what());
        m_running = false;
        rclcpp::shutdown();
      } catch (const cv::Exception& e) {
        CORE_LOG_ERROR(
          "OpenCV exception in AI thread: %s; initiating shutdown",
          e.what());
        m_running = false;
        rclcpp::shutdown();
      } catch (const std::exception& e) {
        CORE_LOG_ERROR(
          "Standard exception in AI thread: %s; initiating shutdown",
          e.what());
        m_running = false;
        rclcpp::shutdown();
      } catch (...) {
        CORE_LOG_ERROR(
          "Unknown critical exception in AI thread; initiating shutdown");
        m_running = false;
        rclcpp::shutdown();
      }
  }
}

}  // namespace presence_detection
