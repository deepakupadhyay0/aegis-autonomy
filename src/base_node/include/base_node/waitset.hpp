#pragma once

#include <array>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <stdexcept>
#include <chrono>

#include <rclcpp/rclcpp.hpp>

namespace base_node
{
namespace topic
{

// Compile-time recursive templated "call".
template<size_t I, typename T, typename ... Ts>
struct nth_type_impl
{
  using type = typename nth_type_impl<I - 1, Ts...>::type;
};


template<typename T, typename ... Ts>
struct nth_type_impl<0U, T, Ts...>
{
  using type = T;
};

/// @brief Provides type of an Nth parameter in a parameter pack.
template<size_t I, typename ... Ts>
using nth_type_t = typename nth_type_impl<I, Ts...>::type;

/// @brief A deterministic ROS 2 waitset wrapper for synchronizing multiple subscriptions.
template<typename ... Msgs>
class waitset_c
{
public:
  waitset_c(
    rclcpp::Node & node,
    std::shared_ptr<rclcpp::Subscription<Msgs>>... sub_ptrs)
  : m_waitset(rcl_get_zero_initialized_wait_set()),
    m_subs{sub_ptrs->get_subscription_handle() ...},
    m_msgs(std::make_tuple(Msgs()...)),
    m_construction_timestamp(node.now()),
    m_last_timestamp(),
    m_sub_has_msg(),
    m_msg_ptrs()
  {
    m_last_timestamp.fill(m_construction_timestamp);
    m_sub_has_msg.fill(false);
    finalize_constructor(node.get_node_options().context()->get_rcl_context());
  }

  template<size_t IDX>
  bool has_new_message() const
  {
    static_assert(IDX < (sizeof...(Msgs)), "Index out of bounds");
    return m_sub_has_msg[IDX];
  }

  template<size_t IDX>
  nth_type_t<IDX, Msgs ...> const & get_message() const
  {
    static_assert(IDX < (sizeof...(Msgs)), "Index out of bounds");
    return std::get<IDX>(m_msgs);
  }

  template<size_t IDX>
  rmw_message_info_t const & get_message_info() const
  {
    static_assert(IDX < (sizeof...(Msgs)), "Index out of bounds");
    return m_msg_info[IDX];
  }

  template<size_t IDX>
  rclcpp::Time get_timestamp_of_last_msg() const
  {
    static_assert(IDX < (sizeof...(Msgs)), "Index out of bounds");
    return m_last_timestamp[IDX];
  }

  rclcpp::Time get_construction_timestamp() const
  {
    return m_construction_timestamp;
  }

  rcl_ret_t wait_for_message(std::chrono::nanoseconds const & timeout)
  {
    m_sub_has_msg.fill(false);
    rcl_ret_t retval = rcl_wait_set_clear(&m_waitset);
    
    if (retval == RCL_RET_OK) {
      for (size_t i = 0U; i < m_subs.size(); ++i) {
        retval = rcl_wait_set_add_subscription(&m_waitset, m_subs[i].get(), nullptr);
        if (retval != RCL_RET_OK) {
          break;
        }
      }

      if (retval == RCL_RET_OK) {
        retval = rcl_wait(&m_waitset, timeout.count());
        if (retval == RCL_RET_OK) {
          if (m_waitset.subscriptions != nullptr) {
            for (size_t i = 0U; i < m_waitset.size_of_subscriptions; ++i) {
              if (m_waitset.subscriptions[i] != nullptr) {
                retval = RCL_RET_OK;
                break;
              }
            }
          } else {
            retval = RCL_RET_WAIT_SET_INVALID;
          }
        }
      }
    }

    if (retval == RCL_RET_OK) {
      for (size_t sub_idx = 0U; sub_idx < m_waitset.size_of_subscriptions; ++sub_idx) {
        auto subscription_ptr = m_waitset.subscriptions[sub_idx];
        if (subscription_ptr != nullptr) {
          size_t idx = m_sub_data_map[subscription_ptr];
          rcl_ret_t result = rcl_take(m_subs[idx].get(), m_msg_ptrs[idx], &m_msg_info[idx], nullptr);
          m_sub_has_msg[idx] = (result == RCL_RET_OK);
          if (m_sub_has_msg[idx]) {
            m_last_timestamp[idx] = rclcpp::Clock().now();
          }
        }
      }
    }
    return retval;
  }

  template<class Rep, class Period>
  rcl_ret_t wait_for_message(const std::chrono::duration<Rep, Period> & timeout)
  {
    return wait_for_message(std::chrono::duration_cast<std::chrono::nanoseconds>(timeout));
  }

  waitset_c(const waitset_c &) = delete;
  waitset_c & operator=(const waitset_c &) = delete;
  waitset_c() = delete;

  virtual ~waitset_c()
  {
    rcl_ret_t retval = rcl_wait_set_fini(&m_waitset);
    if (retval != RCL_RET_OK) {
      RCLCPP_ERROR(rclcpp::get_logger("waitset_c"), "rcl_wait_set_fini failed");
    }
    for (auto sub_ptr : m_subs) {
      sub_ptr.reset();
    }
  }

private:
  void finalize_constructor(std::shared_ptr<rcl_context_t> context)
  {
    rcl_allocator_t allocator = rcl_get_default_allocator();
    rcl_ret_t retval = rcl_wait_set_init(
      &m_waitset,
      m_subs.size(),
      0U, 0U, 0U, 0U, 0U,
      context.get(),
      allocator);
      
    if (retval != RCL_RET_OK) {
      throw std::runtime_error("rcl_wait_set_init failed");
    }

    std::apply([&, this](Msgs & ... tupleArgs){
        m_msg_ptrs = {&tupleArgs ...};
      },
      m_msgs);

    for (size_t i = 0U; i < m_subs.size(); ++i) {
      m_sub_data_map[m_subs[i].get()] = i;
    }
  }

  rcl_wait_set_t m_waitset;
  std::array<std::shared_ptr<rcl_subscription_t>, sizeof...(Msgs)> m_subs;
  std::tuple<Msgs ...> m_msgs;
  std::array<rmw_message_info_t, sizeof...(Msgs)> m_msg_info;
  rclcpp::Time m_construction_timestamp;
  std::array<rclcpp::Time, sizeof...(Msgs)> m_last_timestamp;
  std::array<bool, sizeof...(Msgs)> m_sub_has_msg;
  std::array<void *, sizeof...(Msgs)> m_msg_ptrs;

  std::map<rcl_subscription_t const *, size_t> m_sub_data_map;
};

}  // namespace topic
}  // namespace base_node
