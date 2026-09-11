#pragma once

#include "base_core/waiting_subscriber.hpp"
#include "common/fixed_string.hpp"

#include <rclcpp/rclcpp.hpp>

#include <concepts>
#include <memory>

namespace base_core
{
namespace topic
{

template<
  typename message_t,
  typename node_t,
  typename waiting_subscriber_t = waiting_subscriber_c<message_t>>
requires std::constructible_from<
  waiting_subscriber_t,
  node_t &,
  const common::string256_t &,
  const rclcpp::QoS &,
  rclcpp::IntraProcessSetting>
std::shared_ptr<waiting_subscriber_t> create_waiting_subscriber(
  node_t & node,
  const common::string256_t & topic_name,
  const rclcpp::QoS & qos,
  const rclcpp::IntraProcessSetting intra_process_setting =
  rclcpp::IntraProcessSetting::NodeDefault)
{
  return std::make_shared<waiting_subscriber_t>(
    node,
    topic_name,
    qos,
    intra_process_setting);
}

}  // namespace topic
}  // namespace base_core
