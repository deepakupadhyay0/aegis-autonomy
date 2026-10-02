#include "localization_config.hpp"

#include "common/config_validation.hpp"
#include "common/fixed_string.hpp"
#include "localization/ndt_localizer.hpp"

#include <stdexcept>
#include <string>

namespace localization_ros
{
namespace
{

void validate_config(const autonomy_config::Localization & config)
{
  const autonomy_config::Localization::Ros & ros = config.get_ros();
  if (ros.get_points_topic().empty() ||
    ros.get_points_topic().size() > common::string256_t::capacity())
  {
    throw std::invalid_argument(
            "points_topic is empty or exceeds 256 characters");
  }
  if (ros.get_pose_topic().empty() || ros.get_map_frame().empty()) {
    throw std::invalid_argument(
            "pose_topic and map_frame must not be empty");
  }
  common::validation::require_positive_representable<std::size_t>(
    ros.get_pose_queue_depth(),
    "ros.pose_queue_depth must fit in size_t and be positive");
  localization::validate_ndt_localizer_config(config.get_localizer());

  const common::int64_t device_index =
    config.get_accelerator().get_device_index();
  common::validation::require_representable<common::uint32_t>(
    device_index,
    "accelerator.device_index must fit in uint32");
}

}  // namespace

autonomy_config::Localization load_localization_config(rclcpp::Node & node)
{
  const autonomy_config::Localization file_config =
    autonomy_config::Localization::get_run_time_values();
  const autonomy_config::Localization::Ros & file_ros = file_config.get_ros();
  const autonomy_config::Localization::Ros ros{
    node.declare_parameter<std::string>(
      "points_topic", file_ros.get_points_topic()),
    node.declare_parameter<std::string>(
      "pose_topic", file_ros.get_pose_topic()),
    node.declare_parameter<std::string>(
      "map_frame", file_ros.get_map_frame()),
    node.declare_parameter<common::int64_t>(
      "pose_queue_depth", file_ros.get_pose_queue_depth())};
  const autonomy_config::Localization config{
    ros,
    file_config.get_map(),
    file_config.get_localizer(),
    file_config.get_accelerator()};
  validate_config(config);
  return config;
}

}  // namespace localization_ros
