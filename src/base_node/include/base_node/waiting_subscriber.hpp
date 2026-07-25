#pragma once

#include <memory>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "base_node/ring_buffer.hpp"

namespace base_node
{
namespace topic
{

/// @brief A thread-safe ROS 2 subscriber wrapper that queues incoming messages into a ring buffer.
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
  : m_buffer(queue_size)
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

  ~waiting_subscriber_c() = default;

  void wait_and_pop(MessageT & msg)
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this]() { return !m_buffer.empty(); });
    msg = m_buffer.front();
    m_buffer.pop_front();
  }

  bool wait_and_pop_timeout(MessageT & msg, std::chrono::milliseconds timeout)
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_cv.wait_for(lock, timeout, [this]() { return !m_buffer.empty(); })) {
      msg = m_buffer.front();
      m_buffer.pop_front();
      return true;
    }
    return false;
  }

  size_t size() const
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_buffer.size();
  }

  void clear()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_buffer.clear();
  }

private:
  void callback(const std::shared_ptr<MessageT> msg)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_buffer.push_back(*msg);
    m_cv.notify_one();
  }

  ring_buffer_c<MessageT> m_buffer;
  mutable std::mutex m_mutex;
  std::condition_variable m_cv;
  typename rclcpp::Subscription<MessageT>::SharedPtr m_subscriber;
};

}  // namespace topic
}  // namespace base_node
