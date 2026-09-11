#pragma once

#include "base_core/core_defs.hpp"

#include <rclcpp/rclcpp.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>

namespace base_core
{
namespace execution
{

template<typename ... message_ts>
class waitset_c final
{
  static_assert(sizeof...(message_ts) > 0U, "waitset_c requires a subscription");
  static_assert(
    (std::is_default_constructible_v<message_ts>&& ...),
    "waitset_c message types must be default constructible");

public:
  /// Accepts normal and lifecycle nodes through their common node-base interface.
  template<typename node_t>
  waitset_c(
    node_t & node,
    const std::shared_ptr<rclcpp::Subscription<message_ts>> & ... subscriptions)
  : m_waitset(rcl_get_zero_initialized_wait_set()),
    m_cancel_guard(node.get_node_base_interface()->get_context()),
    m_subscriptions(get_subscription_handles(subscriptions ...)),
    m_messages(),
    m_message_info(),
    m_subscription_has_message(),
    m_message_pointers(),
    m_waitset_indices(),
    m_cancelled(false)
  {
    m_subscription_has_message.fill(false);
    this->initialize(
      node.get_node_base_interface()->get_context()->get_rcl_context());
  }

  ~waitset_c() noexcept
  {
    const rcl_ret_t result = rcl_wait_set_fini(&m_waitset);
    if (result != RCL_RET_OK) {
      static_cast<void>(std::fputs(
          "waitset_c: rcl_wait_set_fini failed\n",
          stderr));
    }
  }

  waitset_c(const waitset_c &) = delete;
  waitset_c & operator=(const waitset_c &) = delete;
  waitset_c(waitset_c &&) = delete;
  waitset_c & operator=(waitset_c &&) = delete;

  template<size_t index>
  bool8_t has_new_message() const noexcept
  {
    static_assert(index < sizeof...(message_ts), "Index out of bounds");
    return m_subscription_has_message[index];
  }

  template<size_t index>
  const std::tuple_element_t<index, std::tuple<message_ts...>> &
  get_message() const noexcept
  {
    static_assert(index < sizeof...(message_ts), "Index out of bounds");
    return std::get<index>(m_messages);
  }

  template<size_t index>
  const rmw_message_info_t & get_message_info() const noexcept
  {
    static_assert(index < sizeof...(message_ts), "Index out of bounds");
    return m_message_info[index];
  }

  rcl_ret_t wait_for_message(const std::chrono::nanoseconds timeout) noexcept
  {
    m_subscription_has_message.fill(false);
    m_message_info.fill(rmw_message_info_t{});

    if (m_cancelled.load()) {
      return RCL_RET_ALREADY_SHUTDOWN;
    }

    rcl_ret_t result = rcl_wait_set_clear(&m_waitset);
    if (result != RCL_RET_OK) {
      return result;
    }

    for (size_t subscription_index = 0U;
      subscription_index < m_subscriptions.size();
      ++subscription_index)
    {
      size_t waitset_index = 0U;
      result = rcl_wait_set_add_subscription(
        &m_waitset,
        m_subscriptions[subscription_index].get(),
        &waitset_index);
      if (result != RCL_RET_OK) {
        return result;
      }
      m_waitset_indices[subscription_index] = waitset_index;
    }

    result = rcl_wait_set_add_guard_condition(
      &m_waitset,
      &m_cancel_guard.get_rcl_guard_condition(),
      nullptr);
    if (result != RCL_RET_OK) {
      return result;
    }

    result = rcl_wait(&m_waitset, timeout.count());
    if (result != RCL_RET_OK) {
      return result;
    }
    if (m_cancelled.load()) {
      return RCL_RET_ALREADY_SHUTDOWN;
    }
    if (m_waitset.subscriptions == nullptr) {
      return RCL_RET_WAIT_SET_INVALID;
    }

    rcl_ret_t first_take_error = RCL_RET_OK;
    for (size_t subscription_index = 0U;
      subscription_index < m_subscriptions.size();
      ++subscription_index)
    {
      const size_t waitset_index = m_waitset_indices[subscription_index];
      if (waitset_index >= m_waitset.size_of_subscriptions) {
        return RCL_RET_WAIT_SET_INVALID;
      }

      const rcl_subscription_t * const ready_subscription =
        m_waitset.subscriptions[waitset_index];
      if (ready_subscription == nullptr) {
        continue;
      }
      if (ready_subscription != m_subscriptions[subscription_index].get()) {
        return RCL_RET_WAIT_SET_INVALID;
      }

      const rcl_ret_t take_result = rcl_take(
        m_subscriptions[subscription_index].get(),
        m_message_pointers[subscription_index],
        &m_message_info[subscription_index],
        nullptr);
      if (take_result == RCL_RET_OK) {
        m_subscription_has_message[subscription_index] = true;
      } else if (first_take_error == RCL_RET_OK) {
        first_take_error = take_result;
      }
    }

    return first_take_error;
  }

  template<class rep_t, class period_t>
  rcl_ret_t wait_for_message(
    const std::chrono::duration<rep_t, period_t> timeout) noexcept
  {
    return this->wait_for_message(
      std::chrono::duration_cast<std::chrono::nanoseconds>(timeout));
  }

  rcl_ret_t cancel() noexcept
  {
    const bool8_t was_cancelled = m_cancelled.exchange(true);
    if (was_cancelled) {
      return RCL_RET_OK;
    }

    try {
      m_cancel_guard.trigger();
    } catch (...) {
      return RCL_RET_ERROR;
    }
    return RCL_RET_OK;
  }

private:
  static std::array<
    std::shared_ptr<rcl_subscription_t>,
    sizeof...(message_ts)> get_subscription_handles(
    const std::shared_ptr<rclcpp::Subscription<message_ts>> & ... subscriptions)
  {
    if (((subscriptions == nullptr) || ...)) {
      throw std::invalid_argument("waitset_c received a null subscription");
    }
    const std::array<
      std::shared_ptr<rcl_subscription_t>,
      sizeof...(message_ts)> handles{
      subscriptions->get_subscription_handle()...};

    for (size_t first_index = 0U; first_index < handles.size(); ++first_index) {
      if (handles[first_index] == nullptr) {
        throw std::runtime_error("waitset_c received an invalid subscription handle");
      }
      for (size_t second_index = first_index + 1U;
        second_index < handles.size();
        ++second_index)
      {
        if (handles[first_index] == handles[second_index]) {
          throw std::invalid_argument("waitset_c received a duplicate subscription");
        }
      }
    }

    return handles;
  }

  void initialize(const std::shared_ptr<rcl_context_t> & context)
  {
    if (context == nullptr) {
      throw std::invalid_argument("waitset_c received a null ROS context");
    }

    const rcl_allocator_t allocator = rcl_get_default_allocator();
    const rcl_ret_t result = rcl_wait_set_init(
      &m_waitset,
      m_subscriptions.size(),
      1U,
      0U,
      0U,
      0U,
      0U,
      context.get(),
      allocator);
    if (result != RCL_RET_OK) {
      throw std::runtime_error("rcl_wait_set_init failed");
    }

    std::apply(
      [this](message_ts & ... messages) {
        m_message_pointers = {static_cast<void *>(&messages)...};
      },
      m_messages);
  }

  rcl_wait_set_t m_waitset;
  rclcpp::GuardCondition m_cancel_guard;
  std::array<
    std::shared_ptr<rcl_subscription_t>,
    sizeof...(message_ts)> m_subscriptions;
  std::tuple<message_ts...> m_messages;
  std::array<rmw_message_info_t, sizeof...(message_ts)> m_message_info;
  std::array<bool8_t, sizeof...(message_ts)> m_subscription_has_message;
  std::array<void *, sizeof...(message_ts)> m_message_pointers;
  std::array<size_t, sizeof...(message_ts)> m_waitset_indices;
  std::atomic<bool8_t> m_cancelled;
};

}  // namespace execution
}  // namespace base_core
