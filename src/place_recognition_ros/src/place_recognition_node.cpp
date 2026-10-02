#include "place_recognition_ros/place_recognition_node.hpp"

#include "place_recognition_config.hpp"
#include "base_core/create_waiting_subscriber.hpp"
#include "common/fixed_string.hpp"
#include "logging/log_macros.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <rcl/error_handling.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace place_recognition_ros
{
namespace
{

constexpr std::int64_t NANOSECONDS_PER_SECOND = 1'000'000'000LL;
constexpr std::int64_t NANOSECONDS_PER_MILLISECOND = 1'000'000LL;

std::int64_t timestamp_ns(const builtin_interfaces::msg::Time & stamp) noexcept
{
  return (static_cast<std::int64_t>(stamp.sec) * NANOSECONDS_PER_SECOND) +
         static_cast<std::int64_t>(stamp.nanosec);
}

}  // namespace

place_recognition_node_c::place_recognition_node_c(
  const std::vector<std::string> & args,
  const base_core::base_node_options_s & options)
: base_core::ros_base_node_c(options)
{
  static_cast<void>(args);
}

place_recognition_node_c::~place_recognition_node_c() noexcept
{
  if (m_image_subscriber != nullptr) {
    static_cast<void>(m_image_subscriber->shutdown());
  }
  if (m_wifi_subscriber != nullptr) {
    static_cast<void>(m_wifi_subscriber->shutdown());
  }
}

void place_recognition_node_c::step1_allocate_resources(
  const std::vector<std::string> & args)
{
  static_cast<void>(args);
  m_config.emplace(load_place_recognition_config(*this));
  const autonomy_config::PlaceRecognition::Ros & ros = m_config->get_ros();
  const autonomy_config::PlaceRecognition::Model & model =
    m_config->get_model();

  common::string256_t image_topic;
  image_topic = ros.get_image_topic();
  rclcpp::SensorDataQoS image_qos;
  image_qos.keep_last(
    static_cast<std::size_t>(ros.get_image_queue_depth()));
  m_image_subscriber =
    base_core::topic::create_waiting_subscriber<image_message_t>(
    *this,
    image_topic,
    image_qos,
    rclcpp::IntraProcessSetting::NodeDefault);

  if (!ros.get_wifi_topic().empty()) {
    common::string256_t wifi_topic;
    wifi_topic = ros.get_wifi_topic();
    rclcpp::SensorDataQoS wifi_qos;
    wifi_qos.keep_last(
      static_cast<std::size_t>(ros.get_wifi_queue_depth()));
    m_wifi_subscriber =
      base_core::topic::create_waiting_subscriber<wifi_message_t>(
      *this,
      wifi_topic,
      wifi_qos,
      rclcpp::IntraProcessSetting::NodeDefault);
  }

  m_descriptor_publisher =
    this->create_publisher<autonomy_msgs::msg::PlaceDescriptor>(
    ros.get_descriptor_topic(),
    rclcpp::QoS(1U));
  m_model = std::make_unique<place_recognition::place_encoder_c>(
    model.get_wifi_feature_count(),
    model.get_access_point_count(),
    model.get_hidden_size(),
    model.get_descriptor_size());
  torch::serialize::InputArchive archive;
  archive.load_from(model.get_checkpoint_path(), torch::Device(torch::kCPU));
  m_model->load(archive);
  if (torch::cuda::is_available()) {
    m_device = torch::Device(torch::kCUDA);
  }
  m_model->to(m_device);
  m_model->eval();
  m_image_mean = torch::tensor({0.485F, 0.456F, 0.406F})
    .view({1, 3, 1, 1})
    .to(m_device);
  m_image_deviation = torch::tensor({0.229F, 0.224F, 0.225F})
    .view({1, 3, 1, 1})
    .to(m_device);

  CORE_LOG_INFO(
    "Place recognition waiting for compressed images on %s; WiFi %s",
    ros.get_image_topic().c_str(),
    m_wifi_subscriber == nullptr ? "disabled" : "optional");
}

void place_recognition_node_c::step2_start_threads(
  const std::vector<std::string> & args)
{
  static_cast<void>(args);
}

void place_recognition_node_c::step3_run_forever(
  const std::vector<std::string> & args)
{
  static_cast<void>(args);
  constexpr std::chrono::milliseconds WAIT_TIMEOUT{250};
  while (this->ok()) {
    const rcl_ret_t wait_status =
      m_image_subscriber->wait_for_message(WAIT_TIMEOUT);
    if (wait_status == RCL_RET_TIMEOUT ||
      wait_status == RCL_RET_SUBSCRIPTION_TAKE_FAILED)
    {
      continue;
    }
    if (!this->ok() || wait_status == RCL_RET_ALREADY_SHUTDOWN) {
      break;
    }
    if (wait_status != RCL_RET_OK) {
      throw std::runtime_error(
              "Image wait failed with rcl status " +
              std::to_string(static_cast<common::int32_t>(wait_status)));
    }

    const image_message_t::ConstSharedPtr image =
      m_image_subscriber->get_message();
    if (image == nullptr) {
      throw std::runtime_error("Image wait succeeded without a message");
    }
    this->process_image(*image);
  }
}

bool place_recognition_node_c::preprocess_image(
  const image_message_t & message,
  torch::Tensor & image) const noexcept
{
  try {
    if (message.data.empty() ||
      message.data.size() > static_cast<std::size_t>(
        std::numeric_limits<std::int32_t>::max()))
    {
      return false;
    }
    const cv::Mat encoded(
      1,
      static_cast<std::int32_t>(message.data.size()),
      CV_8UC1,
      const_cast<common::uint8_t *>(message.data.data()));
    const cv::Mat decoded = cv::imdecode(encoded, cv::IMREAD_COLOR);
    if (decoded.empty()) {
      return false;
    }

    const autonomy_config::PlaceRecognition::Model & model =
      m_config->get_model();
    cv::Mat resized;
    cv::resize(
      decoded,
      resized,
      cv::Size(
        static_cast<std::int32_t>(model.get_image_width()),
        static_cast<std::int32_t>(model.get_image_height())));
    cv::Mat rgb;
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);
    cv::Mat float_image;
    rgb.convertTo(float_image, CV_32FC3, 1.0 / 255.0);
    image = torch::from_blob(
      float_image.data,
      {model.get_image_height(), model.get_image_width(), 3},
      torch::TensorOptions().dtype(torch::kFloat32))
      .permute({2, 0, 1})
      .unsqueeze(0)
      .clone()
      .to(m_device);
    image = (image - m_image_mean) / m_image_deviation;
    return true;
  } catch (...) {
    return false;
  }
}

bool place_recognition_node_c::make_wifi_input(
  const image_message_t & image,
  torch::Tensor & features,
  torch::Tensor & mask)
{
  const autonomy_config::PlaceRecognition::Model & model =
    m_config->get_model();
  features = torch::zeros(
    {
      1,
      model.get_access_point_count(),
      model.get_wifi_feature_count()
    },
    torch::TensorOptions().dtype(torch::kFloat32));
  mask = torch::zeros(
    {1, model.get_access_point_count()},
    torch::TensorOptions().dtype(torch::kBool));
  if (m_wifi_subscriber == nullptr) {
    return false;
  }

  const rcl_ret_t status = m_wifi_subscriber->wait_for_message(
    std::chrono::nanoseconds::zero());
  if (status == RCL_RET_TIMEOUT ||
    status == RCL_RET_SUBSCRIPTION_TAKE_FAILED)
  {
    return false;
  }
  if (status != RCL_RET_OK) {
    CORE_LOG_WARN_THROTTLE(
      std::chrono::seconds(2),
      "WiFi poll failed with rcl status %d",
      static_cast<common::int32_t>(status));
    return false;
  }

  const wifi_message_t::ConstSharedPtr wifi = m_wifi_subscriber->get_message();
  if (wifi == nullptr ||
    static_cast<common::int64_t>(wifi->feature_count) !=
    model.get_wifi_feature_count() ||
    wifi->features.size() != wifi->access_point_indices.size() *
    static_cast<std::size_t>(wifi->feature_count))
  {
    CORE_LOG_WARN_THROTTLE(
      std::chrono::seconds(2),
      "Rejected malformed WiFi observation");
    return false;
  }

  const std::int64_t age_ns = std::abs(
    timestamp_ns(image.header.stamp) - timestamp_ns(wifi->header.stamp));
  if (age_ns > model.get_maximum_wifi_age_ms() *
    NANOSECONDS_PER_MILLISECOND)
  {
    return false;
  }

  torch::TensorAccessor<float, 3> feature_values =
    features.accessor<float, 3>();
  torch::TensorAccessor<bool, 2> present = mask.accessor<bool, 2>();
  for (std::size_t row = 0U;
    row < wifi->access_point_indices.size();
    ++row)
  {
    const common::uint16_t access_point = wifi->access_point_indices[row];
    if (static_cast<common::int64_t>(access_point) >=
      model.get_access_point_count() ||
      present[0][access_point])
    {
      CORE_LOG_WARN_THROTTLE(
        std::chrono::seconds(2),
        "Rejected out-of-range or duplicate WiFi access-point index");
      mask.zero_();
      features.zero_();
      return false;
    }
    present[0][access_point] = true;
    for (std::size_t feature = 0U;
      feature < static_cast<std::size_t>(wifi->feature_count);
      ++feature)
    {
      const common::float32_t value = wifi->features[
        (row * static_cast<std::size_t>(wifi->feature_count)) + feature];
      if (!std::isfinite(value)) {
        mask.zero_();
        features.zero_();
        return false;
      }
      feature_values[0][access_point][feature] = value;
    }
  }
  return mask.any().item<bool>();
}

void place_recognition_node_c::process_image(const image_message_t & image)
{
  torch::Tensor camera_input;
  if (!this->preprocess_image(image, camera_input)) {
    CORE_LOG_WARN_THROTTLE(
      std::chrono::seconds(2),
      "Could not decode compressed camera image");
    return;
  }

  torch::Tensor wifi_features;
  torch::Tensor wifi_mask;
  const bool8_t used_wifi = this->make_wifi_input(
    image, wifi_features, wifi_mask);
  const torch::Tensor camera_available = torch::ones(
    {1}, torch::TensorOptions().dtype(torch::kBool).device(m_device));
  torch::NoGradGuard no_gradient;
  const torch::Tensor descriptor = m_model->forward(
    wifi_features.to(m_device),
    wifi_mask.to(m_device),
    camera_input,
    camera_available)
    .to(torch::kCPU)
    .contiguous();

  autonomy_msgs::msg::PlaceDescriptor output;
  output.header = image.header;
  output.used_camera = true;
  output.used_wifi = used_wifi;
  output.values.reserve(static_cast<std::size_t>(descriptor.size(1)));
  const torch::TensorAccessor<float, 2> values = descriptor.accessor<float, 2>();
  for (std::int64_t index = 0; index < descriptor.size(1); ++index) {
    output.values.push_back(values[0][index]);
  }
  m_descriptor_publisher->publish(std::move(output));
}

}  // namespace place_recognition_ros
