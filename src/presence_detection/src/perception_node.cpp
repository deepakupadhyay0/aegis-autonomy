#include "presence_detection/perception_node.hpp"
#include <cv_bridge/cv_bridge.hpp>
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <iostream>
#include <numeric>
#include <cmath>

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
  m_running = true;

  // Initialize ONNX Runtime
  m_ort_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "DepthAnythingV2");
  
  Ort::SessionOptions session_options;
  session_options.SetIntraOpNumThreads(4);
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
  RCLCPP_INFO(this->get_logger(), "Main thread now listening for camera frames...");

  base_node::topic::waitset_c<sensor_msgs::msg::Image> ws(*this, m_image_sub);

  while (rclcpp::ok()) {
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

void perception_node_c::ai_thread_loop()
{
  RCLCPP_INFO(this->get_logger(), "Background AI Thread started! Running ONNX Inference asynchronously.");

  // Depth Anything V2 input dimensions
  const int input_w = 252; // Downscaled via binning to massively accelerate ONNX inference
  const int input_h = 252;
  const size_t input_tensor_size = 1 * 3 * input_h * input_w;
  std::vector<float> input_tensor_values(input_tensor_size);
  std::vector<int64_t> input_node_dims = {1, 3, input_h, input_w};

  auto last_frame_time = std::chrono::high_resolution_clock::now();
  double smoothed_fps = 0.0;
  int frame_count = 0;

  while (m_running && rclcpp::ok()) {
    auto opt_frame = m_frame_queue->wait_and_pop_front(m_running);
    if (!opt_frame.has_value()) {
      continue;
    }
    
    try {
      cv::Mat frame = opt_frame.value();

      auto now = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double> dt = now - last_frame_time;
      last_frame_time = now;
      
      double inst_fps = 1.0 / dt.count();
      if (frame_count < 5) smoothed_fps = inst_fps;
      else smoothed_fps = 0.9 * smoothed_fps + 0.1 * inst_fps;
      frame_count++;
        // 1. Detect Face and Landmarks
        cv::Mat faces;
        if (m_face_detector) {
          m_face_detector->setInputSize(frame.size());
          m_face_detector->detect(frame, faces);
        }

        bool presence = (faces.rows > 0);
        auto msg_presence = std::make_unique<std_msgs::msg::Bool>();
        msg_presence->data = presence;
        m_presence_pub->publish(std::move(msg_presence));

        // 2. Prepare ONNX Input if model is loaded
        cv::Mat depth_map_colored;
        cv::Mat depth_map_raw;
        if (m_ort_session) {
          cv::Mat resized_img;
          // Use INTER_AREA which acts as a mathematically perfect pixel binning kernel
          cv::resize(frame, resized_img, cv::Size(input_w, input_h), 0, 0, cv::INTER_AREA);
          cv::Mat rgb_img;
          cv::cvtColor(resized_img, rgb_img, cv::COLOR_BGR2RGB);
          rgb_img.convertTo(rgb_img, CV_32FC3, 1.0 / 255.0);

          // Normalize using ImageNet mean/std
          std::vector<float> mean = {0.485f, 0.456f, 0.406f};
          std::vector<float> std = {0.229f, 0.224f, 0.225f};
          
          for (int c = 0; c < 3; ++c) {
            for (int h = 0; h < input_h; ++h) {
              for (int w = 0; w < input_w; ++w) {
                input_tensor_values[c * input_h * input_w + h * input_w + w] =
                  (rgb_img.at<cv::Vec3f>(h, w)[c] - mean[c]) / std[c];
              }
            }
          }

          Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            *m_memory_info, input_tensor_values.data(), input_tensor_size, input_node_dims.data(), input_node_dims.size());

          Ort::AllocatorWithDefaultOptions allocator;
          auto input_name_ptr = m_ort_session->GetInputNameAllocated(0, allocator);
          auto output_name_ptr = m_ort_session->GetOutputNameAllocated(0, allocator);
          const char* input_names[] = {input_name_ptr.get()};
          const char* output_names[] = {output_name_ptr.get()};

          auto output_tensors = m_ort_session->Run(
            Ort::RunOptions{nullptr}, input_names, &input_tensor, 1, output_names, 1);

          float* floatarr = output_tensors.front().GetTensorMutableData<float>();
          depth_map_raw = cv::Mat(input_h, input_w, CV_32F, floatarr).clone();
          
          // Normalize for visualization
          cv::Mat depth_map_vis;
          cv::normalize(depth_map_raw, depth_map_vis, 0, 255, cv::NORM_MINMAX, CV_8U);
          cv::applyColorMap(depth_map_vis, depth_map_colored, cv::COLORMAP_INFERNO);
          cv::resize(depth_map_colored, depth_map_colored, frame.size());
        }

        // 3. Extract Eyes & Calculate Metric Depth
        float metric_depth_meters = -1.0f;
        for (int i = 0; i < faces.rows; i++) {
          float* data = faces.ptr<float>(i);
          float score = data[14];
          if (score < 0.8f) continue;

          // Face Bounding Box
          cv::Rect face(static_cast<int>(data[0]), static_cast<int>(data[1]), static_cast<int>(data[2]), static_cast<int>(data[3]));
          cv::rectangle(frame, face, cv::Scalar(0, 255, 0), 2);
          
          float eye1_x = data[4];
          float eye1_y = data[5];
          float eye2_x = data[6];
          float eye2_y = data[7];

          cv::Point2f right_eye(eye1_x, eye1_y);
          cv::Point2f left_eye(eye2_x, eye2_y);

          // Visualize the metric depth anchor points
          cv::circle(frame, right_eye, 3, cv::Scalar(0, 255, 0), -1);
          cv::circle(frame, left_eye, 3, cv::Scalar(0, 255, 0), -1);
          cv::line(frame, right_eye, left_eye, cv::Scalar(0, 255, 0), 2);

          float pixel_dist = std::sqrt(std::pow(eye2_x - eye1_x, 2) + std::pow(eye2_y - eye1_y, 2));
            
          // Generic camera assumption: 60 deg FOV, approx focal length
          float focal_length = (frame.cols / 2.0f) / std::tan(30.0f * M_PI / 180.0f);
          float real_eye_dist = 0.063f; // 63mm

          // Absolute Metric Depth Z
          metric_depth_meters = (focal_length * real_eye_dist) / pixel_dist;

          // Use the raw ONNX float array to calculate the metric scaling factor
          if (m_ort_session && !depth_map_raw.empty()) {
            float face_cx = face.x + (face.width / 2.0f);
            float face_cy = face.y + (face.height / 2.0f);
            int dx = std::max(0, std::min(static_cast<int>(face_cx * (input_w / static_cast<float>(frame.cols))), input_w - 1));
            int dy = std::max(0, std::min(static_cast<int>(face_cy * (input_h / static_cast<float>(frame.rows))), input_h - 1));

            float rel_depth = depth_map_raw.at<float>(dy, dx);
            float scale_factor = metric_depth_meters * rel_depth;
            
            char scale_text[50];
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
