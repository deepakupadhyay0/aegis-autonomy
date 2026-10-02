#include "localization_ros/localization_node.hpp"

#include "localization_config.hpp"
#include "base_core/create_waiting_subscriber.hpp"
#include "common/fixed_string.hpp"
#include "localization/status.hpp"
#include "logging/log_macros.hpp"

#include <rcl/error_handling.h>
#include <sensor_msgs/point_cloud2_iterator.hpp>

#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace localization_ros
{

localization_node_c::localization_node_c(
  const std::vector<std::string> & args,
  const base_core::base_node_options_s & options)
: base_core::ros_base_node_c(options)
{
  static_cast<void>(args);
}

localization_node_c::~localization_node_c() noexcept
{
  if (m_point_cloud_subscriber != nullptr) {
    static_cast<void>(m_point_cloud_subscriber->shutdown());
  }
}

void localization_node_c::step1_allocate_resources(
  const std::vector<std::string> & args)
{
  static_cast<void>(args);

  m_config.emplace(load_localization_config(*this));
  const autonomy_config::Localization::Ros & ros = m_config->get_ros();
  const autonomy_config::Localization::Localizer & localizer =
    m_config->get_localizer();

  m_scan_points.reserve(
    static_cast<std::size_t>(localizer.get_maximum_scan_points()));
  m_map = std::make_unique<localization::ndt_map_c>(m_config->get_map());
  m_pose_publisher = this->create_publisher<geometry_msgs::msg::PoseStamped>(
    ros.get_pose_topic(),
    rclcpp::QoS(static_cast<std::size_t>(ros.get_pose_queue_depth())));

  common::string256_t points_topic;
  points_topic = ros.get_points_topic();
  m_point_cloud_subscriber =
    base_core::topic::create_waiting_subscriber<point_cloud_message_t>(
    *this,
    points_topic,
    rclcpp::SensorDataQoS(),
    rclcpp::IntraProcessSetting::NodeDefault);

  CORE_LOG_INFO(
    "NDT localization waiting for PointCloud2 on %s",
    ros.get_points_topic().c_str());
}

void localization_node_c::step2_start_threads(
  const std::vector<std::string> & args)
{
  static_cast<void>(args);
}

void localization_node_c::step3_run_forever(
  const std::vector<std::string> & args)
{
  static_cast<void>(args);
  constexpr std::chrono::milliseconds WAIT_TIMEOUT{250};

  while (this->ok()) {
    const rcl_ret_t wait_status =
      m_point_cloud_subscriber->wait_for_message(WAIT_TIMEOUT);
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
              "Point-cloud wait failed with rcl status " +
              std::to_string(static_cast<common::int32_t>(wait_status)));
    }

    const point_cloud_message_t::ConstSharedPtr message =
      m_point_cloud_subscriber->get_message();
    if (message == nullptr) {
      throw std::runtime_error("Point-cloud wait succeeded without a message");
    }
    this->process_point_cloud(*message);
  }
}

bool localization_node_c::convert_point_cloud(
  const point_cloud_message_t & message) noexcept
{
  const autonomy_config::Localization::Localizer & config =
    m_config->get_localizer();
  const std::size_t maximum_scan_points =
    static_cast<std::size_t>(config.get_maximum_scan_points());
  const std::size_t minimum_correspondences =
    static_cast<std::size_t>(config.get_minimum_correspondences());
  const std::size_t width = static_cast<std::size_t>(message.width);
  const std::size_t height = static_cast<std::size_t>(message.height);
  if (height != 0U && width > maximum_scan_points / height) {
    CORE_LOG_WARN_THROTTLE(
      std::chrono::seconds(2),
      "Point cloud exceeds maximum_scan_points");
    return false;
  }
  const std::size_t point_count = width * height;
  if (point_count == 0U ||
    point_count > maximum_scan_points)
  {
    CORE_LOG_WARN_THROTTLE(
      std::chrono::seconds(2),
      "Point cloud is empty or exceeds maximum_scan_points");
    return false;
  }

  m_scan_points.clear();
  try {
    sensor_msgs::PointCloud2ConstIterator<common::float32_t> x(message, "x");
    sensor_msgs::PointCloud2ConstIterator<common::float32_t> y(message, "y");
    sensor_msgs::PointCloud2ConstIterator<common::float32_t> z(message, "z");
    const sensor_msgs::PointCloud2ConstIterator<common::float32_t> end = x.end();
    while (x != end) {
      if (std::isfinite(*x) && std::isfinite(*y) && std::isfinite(*z)) {
        m_scan_points.push_back(localization::point_3f_s{*x, *y, *z});
      }
      ++x;
      ++y;
      ++z;
    }
  } catch (const std::runtime_error & error) {
    CORE_LOG_ERROR_THROTTLE(
      std::chrono::seconds(2),
      "PointCloud2 conversion failed: %s",
      error.what());
    return false;
  } catch (...) {
    CORE_LOG_ERROR_THROTTLE(
      std::chrono::seconds(2),
      "PointCloud2 conversion failed with an unknown error");
    return false;
  }

  if (m_scan_points.size() < minimum_correspondences) {
    CORE_LOG_WARN_THROTTLE(
      std::chrono::seconds(2),
      "Point cloud has too few finite points for NDT");
    return false;
  }
  return true;
}

bool localization_node_c::initialize_map(
  const point_cloud_message_t & message)
{
  const localization::status_e status = m_map->rebuild(m_scan_points);
  if (status != localization::status_e::success) {
    CORE_LOG_WARN_THROTTLE(
      std::chrono::seconds(2),
      "NDT map initialization failed with status %" PRIu32,
      static_cast<common::uint32_t>(status));
    return false;
  }

  m_localizer = std::make_unique<localization::cuda_ndt_localizer_c>(
    *m_map, m_config->get_localizer(), m_config->get_accelerator());
  m_scan_frame = message.header.frame_id;
  m_current_pose = localization::pose_3d_s{};
  this->publish_pose(message.header.stamp, m_current_pose);
  CORE_LOG_INFO(
    "Initialized CUDA NDT map from first scan: %zu points, %zu voxels",
    m_map->source_point_count(),
    m_map->voxel_count());
  return true;
}

void localization_node_c::process_point_cloud(
  const point_cloud_message_t & message)
{
  if (message.header.frame_id.empty()) {
    CORE_LOG_WARN_THROTTLE(
      std::chrono::seconds(2),
      "Point cloud has an empty frame_id");
    return;
  }
  if (!this->convert_point_cloud(message)) {
    return;
  }
  if (m_localizer == nullptr) {
    static_cast<void>(this->initialize_map(message));
    return;
  }
  if (message.header.frame_id != m_scan_frame) {
    CORE_LOG_ERROR_THROTTLE(
      std::chrono::seconds(2),
      "Point-cloud frame changed from %s to %s",
      m_scan_frame.c_str(),
      message.header.frame_id.c_str());
    return;
  }

  localization::localization_result_s result{};
  const localization::status_e status = m_localizer->localize(
    m_scan_points, m_current_pose, result);
  if (status != localization::status_e::success || !result.converged) {
    CORE_LOG_WARN_THROTTLE(
      std::chrono::seconds(2),
      "CUDA NDT rejected scan with status %" PRIu32 " and %zu correspondences",
      static_cast<common::uint32_t>(status),
      result.correspondence_count);
    return;
  }

  m_current_pose = result.pose_map_from_scan;
  this->publish_pose(message.header.stamp, m_current_pose);
  CORE_LOG_INFO_THROTTLE(
    std::chrono::seconds(5),
    "CUDA NDT running: cost %.6f, correspondences %zu, iterations %" PRIu32,
    result.mean_mahalanobis_cost,
    result.correspondence_count,
    result.iteration_count);
}

void localization_node_c::publish_pose(
  const builtin_interfaces::msg::Time & stamp,
  const localization::pose_3d_s & pose)
{
  geometry_msgs::msg::PoseStamped message;
  message.header.stamp = stamp;
  message.header.frame_id = m_config->get_ros().get_map_frame();
  message.pose.position.x = pose.translation.x_m;
  message.pose.position.y = pose.translation.y_m;
  message.pose.position.z = pose.translation.z_m;
  message.pose.orientation.x = pose.rotation.x;
  message.pose.orientation.y = pose.rotation.y;
  message.pose.orientation.z = pose.rotation.z;
  message.pose.orientation.w = pose.rotation.w;
  m_pose_publisher->publish(message);
}

}  // namespace localization_ros
