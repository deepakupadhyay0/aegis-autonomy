#include <gtest/gtest.h>

#include "base_core/execution/waitset.hpp"

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>

class waitset_test_fixture_c : public ::testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    rclcpp::init(0, nullptr);
  }

  static void TearDownTestSuite()
  {
    if (rclcpp::ok()) {
      rclcpp::shutdown();
    }
  }
};

TEST_F(waitset_test_fixture_c, RejectsNullSubscription)
{
  rclcpp::Node::SharedPtr node =
    std::make_shared<rclcpp::Node>("waitset_null_subscription_test");
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription;

  EXPECT_THROW(
    {
      base_core::execution::waitset_c<std_msgs::msg::String> waitset(
        *node,
        subscription);
    },
    std::invalid_argument);
}

TEST_F(waitset_test_fixture_c, CancelWakesAndPermanentlyStopsWait)
{
  rclcpp::Node::SharedPtr node =
    std::make_shared<rclcpp::Node>("waitset_cancel_test");
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription =
    node->create_subscription<std_msgs::msg::String>(
    "waitset_cancel_topic",
    1U,
    [](const std_msgs::msg::String::ConstSharedPtr & message) {
      static_cast<void>(message);
    });
  base_core::execution::waitset_c<std_msgs::msg::String> waitset(
    *node,
    subscription);

  std::future<rcl_ret_t> wait_result = std::async(
    std::launch::async,
    [&waitset]() {
      return waitset.wait_for_message(std::chrono::seconds(5));
    });

  EXPECT_EQ(waitset.cancel(), RCL_RET_OK);
  ASSERT_EQ(
    wait_result.wait_for(std::chrono::seconds(1)),
    std::future_status::ready);
  EXPECT_EQ(wait_result.get(), RCL_RET_ALREADY_SHUTDOWN);
  EXPECT_EQ(
    waitset.wait_for_message(std::chrono::nanoseconds(0)),
    RCL_RET_ALREADY_SHUTDOWN);
}

TEST_F(waitset_test_fixture_c, RejectsDuplicateSubscription)
{
  using duplicate_waitset_t = base_core::execution::waitset_c<
    std_msgs::msg::String,
    std_msgs::msg::String>;

  rclcpp::Node::SharedPtr node =
    std::make_shared<rclcpp::Node>("waitset_duplicate_subscription_test");
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription =
    node->create_subscription<std_msgs::msg::String>(
    "waitset_duplicate_topic",
    1U,
    [](const std_msgs::msg::String::ConstSharedPtr & message) {
      static_cast<void>(message);
    });

  EXPECT_THROW(
    {
      duplicate_waitset_t waitset(
        *node,
        subscription,
        subscription);
    },
    std::invalid_argument);
}
