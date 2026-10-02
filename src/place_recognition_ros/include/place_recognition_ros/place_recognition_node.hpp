#pragma once

#include "autonomy_config/place_recognition.hpp"
#include "autonomy_msgs/msg/place_descriptor.hpp"
#include "autonomy_msgs/msg/wifi_observation.hpp"
#include "base_core/base_node.hpp"
#include "base_core/waiting_subscriber.hpp"
#include "place_recognition/place_encoder.hpp"

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <torch/torch.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace place_recognition_ros
{

class place_recognition_node_c final : public base_core::ros_base_node_c
{
public:
  place_recognition_node_c(
    const std::vector<std::string> & args,
    const base_core::base_node_options_s & options);
  ~place_recognition_node_c() noexcept override;

  place_recognition_node_c(const place_recognition_node_c &) = delete;
  place_recognition_node_c & operator=(const place_recognition_node_c &) = delete;
  place_recognition_node_c(place_recognition_node_c &&) = delete;
  place_recognition_node_c & operator=(place_recognition_node_c &&) = delete;

protected:
  void step1_allocate_resources(const std::vector<std::string> & args) override;
  void step2_start_threads(const std::vector<std::string> & args) override;
  void step3_run_forever(const std::vector<std::string> & args) override;

private:
  using image_message_t = sensor_msgs::msg::CompressedImage;
  using wifi_message_t = autonomy_msgs::msg::WifiObservation;
  using image_subscriber_t =
    base_core::topic::waiting_subscriber_c<image_message_t>;
  using wifi_subscriber_t =
    base_core::topic::waiting_subscriber_c<wifi_message_t>;

  bool preprocess_image(
    const image_message_t & message,
    torch::Tensor & image) const noexcept;
  bool make_wifi_input(
    const image_message_t & image,
    torch::Tensor & features,
    torch::Tensor & mask);
  void process_image(const image_message_t & image);

  std::optional<autonomy_config::PlaceRecognition> m_config;
  std::shared_ptr<image_subscriber_t> m_image_subscriber;
  std::shared_ptr<wifi_subscriber_t> m_wifi_subscriber;
  rclcpp::Publisher<autonomy_msgs::msg::PlaceDescriptor>::SharedPtr
    m_descriptor_publisher;
  std::unique_ptr<place_recognition::place_encoder_c> m_model;
  torch::Device m_device{torch::kCPU};
  torch::Tensor m_image_mean;
  torch::Tensor m_image_deviation;
};

}  // namespace place_recognition_ros
