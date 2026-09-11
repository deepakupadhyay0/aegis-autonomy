#pragma once

#include "base_core/core_defs.hpp"
#include "common/fixed_string.hpp"

#include <rclcpp/rclcpp.hpp>
#include <rclcpp/wait_set.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace base_core
{
namespace topic
{

/// @brief Synchronous reception with shared, immutable message ownership.
///
/// One caller owns reception and getter access. Returned shared pointers may be
/// retained across receive attempts and subscriber destruction, and handed to other
/// threads using the caller's synchronization. Only shutdown() may run concurrently
/// with reception. Call shutdown(), join the receiving thread, and then destroy this
/// object while the ROS context is still valid. This class owns no worker thread.
///
/// Pending messages stay in DDS history or rclcpp's QoS-bounded intra-process
/// buffer. There is no additional message queue. Intra-process delivery preserves
/// the received pointer. DDS take copies into a newly allocated application message;
/// previously exposed messages are never reused. These are not middleware loans.
/// Reception can allocate and block, so it is not a hard real-time API.
template<typename message_t>
class waiting_subscriber_c final
{
public:
  using SharedPtr = std::shared_ptr<waiting_subscriber_c<message_t>>;
  using const_message_ptr_t = typename message_t::ConstSharedPtr;

  template<typename node_t>
  waiting_subscriber_c(
    node_t & node,
    const common::string256_t & topic_name,
    const rclcpp::QoS & qos,
    const rclcpp::IntraProcessSetting intra_process_setting =
    rclcpp::IntraProcessSetting::NodeDefault)
  : m_callback_group(node.create_callback_group(
        rclcpp::CallbackGroupType::MutuallyExclusive, false)),
    m_subscriber(this->create_subscription(node, topic_name, qos, intra_process_setting)),
    m_cancel_guard(std::make_shared<rclcpp::GuardCondition>(
        node.get_node_base_interface()->get_context())),
    m_waitset({}, {}, {}, {}, {}, {}, node.get_node_base_interface()->get_context())
  {
    rclcpp::SubscriptionWaitSetMask mask;
    mask.include_events = false;
    m_waitset.add_subscription(m_subscriber, mask);
    m_waitset.add_guard_condition(m_cancel_guard);
  }

  ~waiting_subscriber_c() noexcept
  {
    static_cast<void>(this->shutdown());
  }

  waiting_subscriber_c(const waiting_subscriber_c &) = delete;
  waiting_subscriber_c & operator=(const waiting_subscriber_c &) = delete;
  waiting_subscriber_c(waiting_subscriber_c &&) = delete;
  waiting_subscriber_c & operator=(waiting_subscriber_c &&) = delete;

  /// Returns OK only after taking a message; clears the previous result first.
  /// Zero polls once; a negative timeout waits until reception or cancellation.
  /// Filtered DDS duplicates and stale readiness are retried within the original
  /// timeout. A poll with readiness but no deliverable message returns TAKE_FAILED.
  /// Allocation and ROS errors are returned as RCL status codes.
  rcl_ret_t wait_for_message(const std::chrono::nanoseconds timeout) noexcept
  {
    m_message.reset();
    m_message_info = rmw_message_info_t{};
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    std::chrono::nanoseconds remaining = timeout;
    try {
      while (!m_cancelled.load()) {
        {
          rclcpp::WaitResult<rclcpp::WaitSet> result = m_waitset.wait(remaining);
          if (m_cancelled.load()) {
            return RCL_RET_ALREADY_SHUTDOWN;
          }
          if (result.kind() == rclcpp::WaitResultKind::Timeout) {
            return RCL_RET_TIMEOUT;
          }
          if (result.kind() == rclcpp::WaitResultKind::Empty) {
            return RCL_RET_WAIT_SET_EMPTY;
          }
          if (this->take_ready_message(result)) {
            return RCL_RET_OK;
          }
        }
        // Destroy the wait result before rebuilding the wait set on the next wait.
        if (timeout == std::chrono::nanoseconds::zero()) {
          return RCL_RET_SUBSCRIPTION_TAKE_FAILED;
        }
        if (timeout > std::chrono::nanoseconds::zero()) {
          const std::chrono::nanoseconds elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start);
          if (elapsed >= timeout) {
            return RCL_RET_TIMEOUT;
          }
          remaining = timeout - elapsed;
        }
      }
      return RCL_RET_ALREADY_SHUTDOWN;
    } catch (const rclcpp::exceptions::RCLErrorBase & error) {
      return error.ret;
    } catch (const std::bad_alloc &) {
      return RCL_RET_BAD_ALLOC;
    } catch (...) {
      return RCL_RET_ERROR;
    }
  }

  template<class rep_t, class period_t>
  rcl_ret_t wait_for_message(
    const std::chrono::duration<rep_t, period_t> timeout) noexcept
  {
    return this->wait_for_message(
      std::chrono::duration_cast<std::chrono::nanoseconds>(timeout));
  }

  /// Indicates whether the most recent receive attempt took a message.
  bool8_t has_new_message() const noexcept
  {
    return m_message != nullptr;
  }

  /// Shares ownership of the latest received message, or returns nullptr after
  /// construction or an unsuccessful receive. Later receives cannot overwrite it.
  const_message_ptr_t get_message() const noexcept
  {
    return m_message;
  }

  /// Returns metadata by value so it can be retained with the owning message.
  /// For intra-process messages only from_intra_process is set; other fields are
  /// zero because that path does not supply DDS metadata. Failed receives clear it.
  rmw_message_info_t get_message_info() const noexcept
  {
    return m_message_info;
  }

  /// Permanently cancels reception and wakes a waiting caller. Resources remain
  /// alive until destruction so an in-flight wait can finish before they are freed.
  [[nodiscard]] rcl_ret_t shutdown() noexcept
  {
    m_cancelled.store(true);
    try {
      // Trigger on repeated calls too, so a failed wakeup can be retried.
      m_cancel_guard->trigger();
      return RCL_RET_OK;
    } catch (const rclcpp::exceptions::RCLErrorBase & error) {
      return error.ret;
    } catch (...) {
      return RCL_RET_ERROR;
    }
  }

private:
  template<typename node_t>
  typename rclcpp::Subscription<message_t>::SharedPtr create_subscription(
    node_t & node,
    const common::string256_t & topic_name,
    const rclcpp::QoS & qos,
    const rclcpp::IntraProcessSetting intra_process_setting)
  {
    rclcpp::SubscriptionOptions options;
    options.callback_group = m_callback_group;
    options.use_intra_process_comm = intra_process_setting;
    const std::string ros_topic_name(topic_name.view());
    return node.template create_subscription<message_t>(
      ros_topic_name,
      qos,
      // The private callback group is never added to an executor. This callback
      // runs synchronously only when take_intra_process() executes its waitable;
      // the non-movable subscriber outlives that invocation.
      [this](const_message_ptr_t message) {
        m_message = std::move(message);
        m_message_info = rmw_message_info_t{};
        m_message_info.from_intra_process = true;
      },
      options);
  }

  bool8_t take_intra_process(const std::shared_ptr<rclcpp::Waitable> & waitable)
  {
    if (waitable == nullptr) {
      return false;
    }
    const std::shared_ptr<void> data = waitable->take_data();
    if (data == nullptr) {
      return false;
    }
    waitable->execute(data);
    m_prefer_intra_process = false;
    return this->has_new_message();
  }

  bool8_t take_dds()
  {
    std::shared_ptr<message_t> message = std::make_shared<message_t>();
    rclcpp::MessageInfo message_info;
    // Subscription::take also filters DDS copies of intra-process deliveries.
    if (!m_subscriber->take(*message, message_info)) {
      return false;
    }
    m_message = std::move(message);
    m_message_info = message_info.get_rmw_message_info();
    m_prefer_intra_process = true;
    return true;
  }

  bool8_t take_ready_message(rclcpp::WaitResult<rclcpp::WaitSet> & result)
  {
    const std::shared_ptr<rclcpp::SubscriptionBase> subscription =
      result.next_ready_subscription();
    // QoS events are excluded, so the only possible waitable is intra-process.
    const std::shared_ptr<rclcpp::Waitable> waitable = result.next_ready_waitable();
    // Alternate after successful deliveries to avoid starving either source.
    if (m_prefer_intra_process && this->take_intra_process(waitable)) {
      return true;
    }
    if (subscription != nullptr && this->take_dds()) {
      return true;
    }
    return !m_prefer_intra_process && this->take_intra_process(waitable);
  }

  const_message_ptr_t m_message;
  rmw_message_info_t m_message_info{};
  bool8_t m_prefer_intra_process{true};
  std::atomic<bool8_t> m_cancelled{false};
  // Destruction order keeps the subscription and callback group alive until the
  // wait set has released its native resources.
  const rclcpp::CallbackGroup::SharedPtr m_callback_group;
  const typename rclcpp::Subscription<message_t>::SharedPtr m_subscriber;
  const std::shared_ptr<rclcpp::GuardCondition> m_cancel_guard;
  rclcpp::WaitSet m_waitset;
};

}  // namespace topic
}  // namespace base_core
