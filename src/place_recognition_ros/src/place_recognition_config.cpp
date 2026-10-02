#include "place_recognition_config.hpp"

#include "common/config_validation.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace place_recognition_ros
{
namespace
{

void validate_config(const autonomy_config::PlaceRecognition & config)
{
  const autonomy_config::PlaceRecognition::Ros & ros = config.get_ros();
  const autonomy_config::PlaceRecognition::Model & model = config.get_model();
  if (ros.get_image_topic().empty() || ros.get_descriptor_topic().empty()) {
    throw std::invalid_argument(
            "image_topic and descriptor_topic must not be empty");
  }
  if (model.get_checkpoint_path().empty()) {
    throw std::invalid_argument("model.checkpoint_path must not be empty");
  }
  common::validation::require_positive_representable<std::size_t>(
    ros.get_image_queue_depth(),
    "ros.image_queue_depth must fit in size_t and be positive");
  common::validation::require_positive_representable<std::size_t>(
    ros.get_wifi_queue_depth(),
    "ros.wifi_queue_depth must fit in size_t and be positive");
  common::validation::require_in_closed_range<common::int64_t>(
    model.get_wifi_feature_count(),
    1,
    16,
    "model.wifi_feature_count must be in [1, 16]");
  common::validation::require_in_closed_range<common::int64_t>(
    model.get_access_point_count(),
    1,
    64,
    "model.access_point_count must be in [1, 64]");
  common::validation::require_in_closed_range<common::int64_t>(
    model.get_hidden_size(),
    1,
    4096,
    "model.hidden_size must be in [1, 4096]");
  common::validation::require_in_closed_range<common::int64_t>(
    model.get_descriptor_size(),
    1,
    256,
    "model.descriptor_size must be in [1, 256]");
  common::validation::require_in_closed_range<common::int64_t>(
    model.get_image_width(),
    32,
    4096,
    "model.image_width must be in [32, 4096]");
  common::validation::require_in_closed_range<common::int64_t>(
    model.get_image_height(),
    32,
    4096,
    "model.image_height must be in [32, 4096]");
  common::validation::require_in_closed_range<common::int64_t>(
    model.get_maximum_wifi_age_ms(),
    0,
    60000,
    "model.maximum_wifi_age_ms must be in [0, 60000]");
  if (model.get_wifi_feature_count() * model.get_access_point_count() > 1024) {
    throw std::invalid_argument(
            "Configured WiFi tensor exceeds WifiObservation.features capacity");
  }
}

}  // namespace

autonomy_config::PlaceRecognition load_place_recognition_config(
  rclcpp::Node & node)
{
  const autonomy_config::PlaceRecognition file_config =
    autonomy_config::PlaceRecognition::get_run_time_values();
  const autonomy_config::PlaceRecognition::Ros & file_ros =
    file_config.get_ros();
  const autonomy_config::PlaceRecognition::Model & file_model =
    file_config.get_model();
  const autonomy_config::PlaceRecognition::Ros ros{
    node.declare_parameter<std::string>(
      "image_topic", file_ros.get_image_topic()),
    node.declare_parameter<std::string>(
      "wifi_topic", file_ros.get_wifi_topic()),
    node.declare_parameter<std::string>(
      "descriptor_topic", file_ros.get_descriptor_topic()),
    node.declare_parameter<common::int64_t>(
      "image_queue_depth", file_ros.get_image_queue_depth()),
    node.declare_parameter<common::int64_t>(
      "wifi_queue_depth", file_ros.get_wifi_queue_depth())};
  const autonomy_config::PlaceRecognition::Model model{
    node.declare_parameter<std::string>(
      "checkpoint_path", file_model.get_checkpoint_path()),
    file_model.get_wifi_feature_count(),
    file_model.get_access_point_count(),
    file_model.get_hidden_size(),
    file_model.get_descriptor_size(),
    file_model.get_image_width(),
    file_model.get_image_height(),
    file_model.get_maximum_wifi_age_ms()};
  const autonomy_config::PlaceRecognition config{ros, model};
  validate_config(config);
  return config;
}

}  // namespace place_recognition_ros
