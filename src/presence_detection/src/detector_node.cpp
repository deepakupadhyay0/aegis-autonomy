#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "std_msgs/msg/bool.hpp"
#include "cv_bridge/cv_bridge.hpp"

#include <opencv2/opencv.hpp>

using namespace std::chrono_literals;

class PresenceDetectorNode : public rclcpp::Node
{
public:
  PresenceDetectorNode()
  : Node("presence_detector_node")
  {
    // Publishers
    image_pub_ = this->create_publisher<sensor_msgs::msg::Image>("camera/image_raw", 10);
    presence_pub_ = this->create_publisher<std_msgs::msg::Bool>("presence_detected", 10);

    // Open camera
    cap_.open(0);
    if (!cap_.isOpened()) {
      RCLCPP_ERROR(this->get_logger(), "Could not open laptop camera.");
    }

    // Load Haar cascade
    std::string cascade_path = "/usr/share/opencv4/haarcascades/haarcascade_frontalface_default.xml";
    if (!face_cascade_.load(cascade_path)) {
      RCLCPP_WARN(this->get_logger(), "Failed to load haar cascade from %s", cascade_path.c_str());
      cascade_path = "/usr/local/share/opencv4/haarcascades/haarcascade_frontalface_default.xml";
      if (!face_cascade_.load(cascade_path)) {
        RCLCPP_ERROR(this->get_logger(), "Failed to load haar cascade.");
      }
    }

    // Timer for processing frames
    timer_ = this->create_wall_timer(
      100ms, std::bind(&PresenceDetectorNode::timer_callback, this));
      
    RCLCPP_INFO(this->get_logger(), "Presence Detector Node has been started (C++).");
  }

  ~PresenceDetectorNode()
  {
    if (cap_.isOpened()) {
      cap_.release();
    }
  }

private:
  void timer_callback()
  {
    if (!cap_.isOpened()) {
      return;
    }

    cv::Mat frame;
    cap_ >> frame;

    if (frame.empty()) {
      RCLCPP_WARN(this->get_logger(), "Failed to capture frame from camera.");
      return;
    }

    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

    // Detect faces
    std::vector<cv::Rect> faces;
    if (!face_cascade_.empty()) {
      face_cascade_.detectMultiScale(gray, faces, 1.1, 5, 0, cv::Size(30, 30));
    }

    bool presence = !faces.empty();

    // Publish presence
    auto msg_presence = std_msgs::msg::Bool();
    msg_presence.data = presence;
    presence_pub_->publish(msg_presence);

    // Draw feedback
    for (const auto& face : faces) {
      cv::rectangle(frame, face, cv::Scalar(0, 255, 0), 2);
    }

    std::string status_text = presence ? "Presence: Detected" : "Presence: Not Detected";
    cv::Scalar color = presence ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255);
    cv::putText(frame, status_text, cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1, color, 2);

    // Publish image
    auto msg_image = cv_bridge::CvImage(std_msgs::msg::Header(), "bgr8", frame).toImageMsg();
    image_pub_->publish(*msg_image);
  }

  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr image_pub_;
  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr presence_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
  
  cv::VideoCapture cap_;
  cv::CascadeClassifier face_cascade_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<PresenceDetectorNode>());
  rclcpp::shutdown();
  return 0;
}
