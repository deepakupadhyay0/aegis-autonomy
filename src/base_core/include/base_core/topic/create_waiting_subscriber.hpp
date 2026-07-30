#pragma once

#include "base_core/waiting_subscriber.hpp"
#include "common/fixed_string.hpp"

#include <rclcpp/rclcpp.hpp>

#include <concepts>
#include <cstddef>
#include <memory>
#include <string>

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
  std::size_t>
std::shared_ptr<waiting_subscriber_t> create_waiting_subscriber(
  node_t & node,
  const common::string256_t & topic_name,
  const rclcpp::QoS & qos,
  const std::size_t queue_size = 10U)
{
  return std::make_shared<waiting_subscriber_t>(
    node,
    topic_name,
    qos,
    queue_size);
}

}  // namespace topic
}  // namespace base_core
