#pragma once

#include <memory>
#include <chrono>
#include <string>
#include <atomic>
#include <optional>

#include "rclcpp/rclcpp.hpp"
#include "base_node/concurrent_ring_buffer.hpp"

namespace base_node
{
namespace topic
{

/// @brief A thread-safe ROS 2 subscriber wrapper that queues incoming messages into a concurrent ring buffer.
/// Allows a main execution loop to block and pop messages deterministically.
template<typename MessageT>
class waiting_subscriber_c
{
public:
  using SharedPtr = std::shared_ptr<waiting_subscriber_c<MessageT>>;

  waiting_subscriber_c(
    rclcpp::Node * node,
    const std::string & topic_name,
    const rclcpp::QoS & qos,
    size_t queue_size = 10U)
  : m_buffer(queue_size),
    m_running(true)
  {
    m_subscriber = node->create_subscription<MessageT>(
      topic_name,
      qos,
      std::bind(&waiting_subscriber_c::callback, this, std::placeholders::_1)
    );
  }

  waiting_subscriber_c(const waiting_subscriber_c &) = delete;
  waiting_subscriber_c & operator=(const waiting_subscriber_c &) = delete;
  waiting_subscriber_c(waiting_subscriber_c &&) = delete;
  waiting_subscriber_c & operator=(waiting_subscriber_c &&) = delete;

  ~waiting_subscriber_c()
  {
    m_running.store(false);
    m_buffer.shutdown();
  }

  void wait_and_pop(MessageT & msg)
  {
    auto opt_val = m_buffer.wait_and_pop_front(m_running);
    if (opt_val.has_value()) {
      msg = *opt_val;
    }
  }

  bool wait_and_pop_timeout(MessageT & msg, std::chrono::milliseconds timeout)
  {
    auto opt_val = m_buffer.wait_and_pop_front_timeout(timeout, m_running);
    if (opt_val.has_value()) {
      msg = *opt_val;
      return true;
    }
    return false;
  }

  size_t size() const
  {
    return m_buffer.size();
  }

  bool empty() const
  {
    return m_buffer.empty();
  }

  void clear()
  {
    m_buffer.clear();
  }

private:
  void callback(const std::shared_ptr<MessageT> msg)
  {
    m_buffer.push_back(*msg);
  }

  concurrent_ring_buffer_c<MessageT> m_buffer;
  std::atomic<bool> m_running;
  typename rclcpp::Subscription<MessageT>::SharedPtr m_subscriber;
};

}  // namespace topic
}  // namespace base_node
