#include "presence_detection/perception_node.hpp"
#include <cv_bridge/cv_bridge.hpp>
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <iostream>
#include <numeric>
#include <cmath>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>

namespace presence_detection
{

perception_node_c::perception_node_c(const std::vector<std::string> & args)
: base_node_c()
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
  
  m_presence_pub = this->create_publisher<std_msgs::msg::Bool>("presence_detected", 10);
  m_debug_image_pub = this->create_publisher<sensor_msgs::msg::Image>("camera/image_debug", 10);
  m_depth_image_pub = this->create_publisher<sensor_msgs::msg::Image>("camera/depth_debug", 10);
  
  m_image_sub = this->create_subscription<sensor_msgs::msg::Image>(
    "camera/image_raw", 10, [](const sensor_msgs::msg::Image::SharedPtr){});

  std::string pkg_share_dir = ament_index_cpp::get_package_share_directory("presence_detection");
  std::string yunet_model_path = pkg_share_dir + "/models/face_detection_yunet.onnx";
  try {
    m_face_detector = cv::FaceDetectorYN::create(yunet_model_path, "", cv::Size(320, 320), 0.9f, 0.3f, 5000);
    RCLCPP_INFO(this->get_logger(), "YuNet Face Detector loaded successfully!");
  } catch (const std::exception& e) {
    RCLCPP_ERROR(this->get_logger(), "Failed to load YuNet: %s", e.what());
  }

  m_frame_queue = std::make_unique<base_node::topic::concurrent_ring_buffer_c<cv::Mat>>(2);

  if (m_client_sock.connect_to_server("@presence_detection_ipc") == core_ret_e::ok) {
    RCLCPP_INFO(this->get_logger(), "Connected to IPC Server via abstract RAM socket! Waiting for SHM fd handshake...");
    base_node::ipc::ipc_message_t handshake = {};
    int32_t shm_fd = -1;
    if (m_client_sock.receive_message(handshake, &shm_fd) == core_ret_e::ok && shm_fd >= 0) {
      m_shm_buf = std::make_unique<base_node::ipc::shm_ring_buffer_c>(3, 1920 * 1080 * 3);
      if (m_shm_buf->attach_from_fd(shm_fd) == core_ret_e::ok) {
        m_using_ipc = true;
        RCLCPP_INFO(this->get_logger(), "Successfully attached to zero-copy SHM ring buffer over IPC!");
      }
    }
  }

  if (!m_using_ipc) {
    RCLCPP_WARN(this->get_logger(), "IPC handshake failed or unavailable. Falling back to DDS topic subscription.");
  }

  m_running = true;

  // Initialize ONNX Runtime
  m_ort_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "DepthAnythingV2");
  
  Ort::SessionOptions session_options;
  const uint32_t hw_cores = std::thread::hardware_concurrency();
  const int32_t optimal_threads = static_cast<int32_t>(std::max(1U, hw_cores / 4U));
  session_options.SetIntraOpNumThreads(optimal_threads);
  RCLCPP_INFO(this->get_logger(), "ONNX Runtime configured dynamically with %d intra-op threads (HW Concurrency: %u)", optimal_threads, hw_cores);
  session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

  // Enable CUDA if available, otherwise fallback to CPU
  try {
    OrtCUDAProviderOptions cuda_options;
    cuda_options.device_id = 0;
    session_options.AppendExecutionProvider_CUDA(cuda_options);
    RCLCPP_INFO(this->get_logger(), "ONNX Runtime: CUDA Execution Provider Enabled!");
  } catch (const std::exception& e) {
    RCLCPP_WARN(this->get_logger(), "CUDA Execution Provider failed to load, falling back to CPU: %s", e.what());
  }

  std::string model_path = pkg_share_dir + "/models/depth_anything_v2.onnx";
  try {
    m_ort_session = std::make_unique<Ort::Session>(*m_ort_env, model_path.c_str(), session_options);
    m_memory_info = std::make_unique<Ort::MemoryInfo>(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));
    RCLCPP_INFO(this->get_logger(), "Depth Anything V2 Small (vits) Model loaded successfully!");
  } catch (const std::exception& e) {
    RCLCPP_ERROR(this->get_logger(), "Failed to load ONNX model: %s", e.what());
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
  RCLCPP_INFO(this->get_logger(), "Main thread now listening for camera frames (IPC Mode: %s)...", m_using_ipc ? "ENABLED" : "FALLBACK DDS");

  base_node::topic::waitset_c<sensor_msgs::msg::Image> ws(*this, m_image_sub);

  while (rclcpp::ok() && m_running.load()) {
    if (m_using_ipc && m_shm_buf && m_shm_buf->is_valid()) {
      base_node::ipc::ipc_message_t msg = {};
      if (m_client_sock.receive_message(msg) == core_ret_e::ok) {
        if (msg.msg_id == 1 && msg.slot_index < m_shm_buf->get_num_slots()) {
          void * slot_ptr = m_shm_buf->get_slot_pointer(msg.slot_index);
          if (slot_ptr != nullptr) {
            cv::Mat shm_frame(static_cast<int>(msg.height), static_cast<int>(msg.width), CV_8UC3, slot_ptr);
            cv::Mat frame = shm_frame.clone();
            if (!m_frame_queue->push_back(frame)) {
              m_frame_queue->pop_front();
              m_frame_queue->push_back(frame);
            }
          }
        }
      } else {
        RCLCPP_WARN(this->get_logger(), "IPC peer disconnected or socket error. Falling back to DDS.");
        m_using_ipc = false;
      }
    } else {
      auto ret = ws.wait_for_message(std::chrono::milliseconds(100));
      if (ret == RCL_RET_OK && ws.has_new_message<0>()) {
        const auto & msg = ws.get_message<0>();
        try {
          cv_bridge::CvImagePtr cv_ptr = cv_bridge::toCvCopy(msg, "bgr8");
          cv::Mat frame = cv_ptr->image;
          
          if (!m_frame_queue->push_back(frame)) {
            m_frame_queue->pop_front();
            m_frame_queue->push_back(frame);
          }
        } catch (cv_bridge::Exception& e) {
          RCLCPP_ERROR(this->get_logger(), "cv_bridge exception: %s", e.what());
        }
      }
    }
  }
}

void perception_node_c::ai_thread_loop()
{
  RCLCPP_INFO(this->get_logger(), "Background AI Thread started! Running ONNX Inference asynchronously.");

  // Depth Anything V2 input dimensions
  const int32_t input_w = 512; // Downscaled via binning
  const int32_t input_h = 512;
  const size_t input_tensor_size = 1U * 3U * static_cast<size_t>(input_h) * static_cast<size_t>(input_w);
  std::vector<int64_t> input_node_dims = {1, 3, input_h, input_w};

  auto last_frame_time = std::chrono::high_resolution_clock::now();
  float64_t smoothed_fps = 0.0;
  int32_t frame_count = 0;

  while (m_running && rclcpp::ok()) {
    std::optional<cv::Mat> opt_frame = m_frame_queue->wait_and_pop_front(m_running);
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
        std::unique_ptr<std_msgs::msg::Bool> msg_presence = std::make_unique<std_msgs::msg::Bool>();
        msg_presence->data = presence;
        m_presence_pub->publish(std::move(msg_presence));

        // 2. Prepare ONNX Input conditionally (only if model is loaded AND presence is detected)
        cv::Mat depth_map_colored;
        cv::Mat depth_map_raw;
        if (presence && m_ort_session) {
          cv::Mat resized_img;
          cv::resize(frame, resized_img, cv::Size(input_w, input_h), 0, 0, cv::INTER_AREA);

          cv::Mat blob = cv::dnn::blobFromImage(
            resized_img,
            1.0 / 255.0,
            cv::Size(input_w, input_h),
            cv::Scalar(0.485 * 255.0, 0.456 * 255.0, 0.406 * 255.0),
            true,
            false);

          // Vectorized channel-wise division by std deviation
          const size_t plane_size = static_cast<size_t>(input_h) * static_cast<size_t>(input_w);
          float32_t* const blob_ptr = blob.ptr<float32_t>();
          cv::Mat channel_r(input_h, input_w, CV_32F, blob_ptr);
          cv::Mat channel_g(input_h, input_w, CV_32F, blob_ptr + plane_size);
          cv::Mat channel_b(input_h, input_w, CV_32F, blob_ptr + (2 * plane_size));

          channel_r *= (1.0f / 0.229f);
          channel_g *= (1.0f / 0.224f);
          channel_b *= (1.0f / 0.225f);

          Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            *m_memory_info, blob_ptr, input_tensor_size, input_node_dims.data(), input_node_dims.size());

          Ort::AllocatorWithDefaultOptions allocator;
          Ort::AllocatedStringPtr input_name_ptr = m_ort_session->GetInputNameAllocated(0, allocator);
          Ort::AllocatedStringPtr output_name_ptr = m_ort_session->GetOutputNameAllocated(0, allocator);
          const char* const input_names[] = {input_name_ptr.get()};
          const char* const output_names[] = {output_name_ptr.get()};

          std::vector<Ort::Value> output_tensors = m_ort_session->Run(
            Ort::RunOptions{nullptr}, input_names, &input_tensor, 1, output_names, 1);

          float32_t* const floatarr = output_tensors.front().GetTensorMutableData<float>();
          depth_map_raw = cv::Mat(input_h, input_w, CV_32F, floatarr).clone();
          
          // Normalize for visualization
          cv::Mat depth_map_vis;
          cv::normalize(depth_map_raw, depth_map_vis, 0, 255, cv::NORM_MINMAX, CV_8U);
          cv::applyColorMap(depth_map_vis, depth_map_colored, cv::COLORMAP_INFERNO);
          cv::resize(depth_map_colored, depth_map_colored, frame.size());
        }

        // 3. Extract Eyes & Calculate Metric Depth
        float32_t metric_depth_meters = -1.0f;
        for (int32_t i = 0; i < faces.rows; i++) {
          const float32_t* const data = faces.ptr<float32_t>(i);
          const float32_t score = data[14];
          if (score < 0.8f) continue;

          const cv::Rect face(static_cast<int32_t>(data[0]), static_cast<int32_t>(data[1]), static_cast<int32_t>(data[2]), static_cast<int32_t>(data[3]));
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

          // Use the raw ONNX float array to calculate the metric scaling factor
          if (m_ort_session && !depth_map_raw.empty()) {
            const float32_t face_cx = face.x + (face.width / 2.0f);
            const float32_t face_cy = face.y + (face.height / 2.0f);
            const int32_t dx = std::max(0, std::min(static_cast<int32_t>(face_cx * (input_w / static_cast<float32_t>(frame.cols))), input_w - 1));
            const int32_t dy = std::max(0, std::min(static_cast<int32_t>(face_cy * (input_h / static_cast<float32_t>(frame.rows))), input_h - 1));

            const float32_t rel_depth = depth_map_raw.at<float32_t>(dy, dx);
            const float32_t scale_factor = metric_depth_meters * rel_depth;
            
            char8_t scale_text[50];
            snprintf(scale_text, sizeof(scale_text), "Scale: %.2f", scale_factor);
            cv::putText(frame, scale_text, cv::Point(10, 110), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 255), 2);
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
        RCLCPP_ERROR(this->get_logger(), "cv_bridge exception: %s. Initiating graceful shutdown.", e.what());
        m_running = false;
        rclcpp::shutdown();
      } catch (const cv::Exception& e) {
        RCLCPP_ERROR(this->get_logger(), "OpenCV exception in AI thread: %s. Initiating graceful shutdown.", e.what());
        m_running = false;
        rclcpp::shutdown();
      } catch (const std::exception& e) {
        RCLCPP_ERROR(this->get_logger(), "Standard exception in AI thread: %s. Initiating graceful shutdown.", e.what());
        m_running = false;
        rclcpp::shutdown();
      } catch (...) {
        RCLCPP_ERROR(this->get_logger(), "Unknown critical exception in AI thread! Initiating graceful shutdown.");
        m_running = false;
        rclcpp::shutdown();
      }
  }
}

}  // namespace presence_detection
