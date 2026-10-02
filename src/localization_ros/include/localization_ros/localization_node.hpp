#pragma once

#include "autonomy_config/localization.hpp"
#include "base_core/base_node.hpp"
#include "base_core/waiting_subscriber.hpp"
#include "common/numeric_types.hpp"
#include "localization/cuda_ndt_localizer.hpp"
#include "localization/ndt_map.hpp"
#include "localization/types.hpp"

#include <builtin_interfaces/msg/time.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace localization_ros
{

class localization_node_c final : public base_core::ros_base_node_c
{
public:
  localization_node_c(
    const std::vector<std::string> & args,
    const base_core::base_node_options_s & options);
  ~localization_node_c() noexcept override;

  localization_node_c(const localization_node_c &) = delete;
  localization_node_c & operator=(const localization_node_c &) = delete;
  localization_node_c(localization_node_c &&) = delete;
  localization_node_c & operator=(localization_node_c &&) = delete;

protected:
  void step1_allocate_resources(const std::vector<std::string> & args) override;
  void step2_start_threads(const std::vector<std::string> & args) override;
  void step3_run_forever(const std::vector<std::string> & args) override;

private:
  using point_cloud_message_t = sensor_msgs::msg::PointCloud2;
  using point_cloud_subscriber_t =
    base_core::topic::waiting_subscriber_c<point_cloud_message_t>;

  bool convert_point_cloud(const point_cloud_message_t & message) noexcept;
  void process_point_cloud(const point_cloud_message_t & message);
  bool initialize_map(const point_cloud_message_t & message);
  void publish_pose(
    const builtin_interfaces::msg::Time & stamp,
    const localization::pose_3d_s & pose);

  std::optional<autonomy_config::Localization> m_config;
  std::shared_ptr<point_cloud_subscriber_t> m_point_cloud_subscriber;
  rclcpp::Publisher<geometry_msgs::msg::PoseStamped>::SharedPtr m_pose_publisher;
  std::unique_ptr<localization::ndt_map_c> m_map;
  std::unique_ptr<localization::cuda_ndt_localizer_c> m_localizer;
  std::vector<localization::point_3f_s> m_scan_points;
  localization::pose_3d_s m_current_pose{};
  std::string m_scan_frame;
};

}  // namespace localization_ros
