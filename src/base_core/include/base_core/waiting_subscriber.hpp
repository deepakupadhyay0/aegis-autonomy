#pragma once

#include "base_core/concurrent_ring_buffer.hpp"
#include "common/fixed_string.hpp"
#include "rclcpp/rclcpp.hpp"

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace base_core
{
namespace topic
{

/// @brief ROS 2 subscriber with a bounded latest-message queue and blocking reads.
template<typename message_t>
class waiting_subscriber_c final
{
public:
  using SharedPtr = std::shared_ptr<waiting_subscriber_c<message_t>>;

  template<typename node_t>
  waiting_subscriber_c(
    node_t & node,
    const common::string256_t & topic_name,
    const rclcpp::QoS & qos,
    const std::size_t queue_size = 10U)
  : m_buffer(queue_size),
    m_subscriber()
  {
    const std::string ros_topic_name(topic_name.view());
    m_subscriber = node.template create_subscription<message_t>(
      ros_topic_name,
      qos,
      [this](typename message_t::UniquePtr message) {
        if (message) {
          m_buffer.push_back(std::move(*message));
        }
      });
  }

  ~waiting_subscriber_c() noexcept
  {
    this->shutdown();
  }

  waiting_subscriber_c(const waiting_subscriber_c &) = delete;
  waiting_subscriber_c & operator=(const waiting_subscriber_c &) = delete;
  waiting_subscriber_c(waiting_subscriber_c &&) = delete;
  waiting_subscriber_c & operator=(waiting_subscriber_c &&) = delete;

  std::optional<message_t> wait_and_pop()
  {
    return m_buffer.wait_and_pop_front();
  }

  std::optional<message_t> wait_and_pop_timeout(
    const std::chrono::milliseconds timeout)
  {
    return m_buffer.wait_and_pop_front_timeout(timeout);
  }

  std::size_t size() const
  {
    return m_buffer.size();
  }

  bool8_t empty() const
  {
    return m_buffer.empty();
  }

  void clear()
  {
    m_buffer.clear();
  }

  void shutdown() noexcept
  {
    m_buffer.shutdown();
    m_subscriber.reset();
  }

private:
  concurrent_ring_buffer_c<message_t> m_buffer;
  typename rclcpp::Subscription<message_t>::SharedPtr m_subscriber;
};

}  // namespace topic
}  // namespace base_core
