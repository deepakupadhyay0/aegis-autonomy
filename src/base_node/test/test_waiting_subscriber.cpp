#include <gtest/gtest.h>

#include "base_node/topic/create_waiting_subscriber.hpp"

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <std_msgs/msg/string.hpp>

#include <concepts>
#include <memory>
#include <string>
#include <utility>

namespace
{

using message_t = std_msgs::msg::String;
using waiting_subscriber_t =
  base_node::topic::waiting_subscriber_c<message_t>;

static_assert(
  std::same_as<
    decltype(
      base_node::topic::create_waiting_subscriber<message_t>(
        std::declval<rclcpp::Node &>(),
        std::declval<const common::string256_t &>(),
        std::declval<const rclcpp::QoS &>())),
    std::shared_ptr<waiting_subscriber_t>>);

static_assert(
  std::same_as<
    decltype(
      base_node::topic::create_waiting_subscriber<message_t>(
        std::declval<rclcpp_lifecycle::LifecycleNode &>(),
        std::declval<const common::string256_t &>(),
        std::declval<const rclcpp::QoS &>())),
    std::shared_ptr<waiting_subscriber_t>>);

class waiting_subscriber_test_fixture_c : public ::testing::Test
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

}  // namespace

TEST_F(waiting_subscriber_test_fixture_c, CreatesDefaultSubscriber)
{
  rclcpp::Node::SharedPtr node =
    std::make_shared<rclcpp::Node>("waiting_subscriber_factory_test");
  std::shared_ptr<waiting_subscriber_t> subscriber =
    base_node::topic::create_waiting_subscriber<message_t>(
    *node,
    "waiting_subscriber_test_topic",
    rclcpp::QoS(1U));

  ASSERT_NE(subscriber, nullptr);
  EXPECT_TRUE(subscriber->empty());
  EXPECT_EQ(subscriber->size(), 0U);
  subscriber->shutdown();
}

TEST_F(waiting_subscriber_test_fixture_c, CreatesLifecycleSubscriber)
{
  rclcpp_lifecycle::LifecycleNode::SharedPtr node =
    std::make_shared<rclcpp_lifecycle::LifecycleNode>(
    "lifecycle_waiting_subscriber_factory_test");
  std::shared_ptr<waiting_subscriber_t> subscriber =
    base_node::topic::create_waiting_subscriber<message_t>(
    *node,
    "lifecycle_waiting_subscriber_test_topic",
    rclcpp::QoS(1U));

  ASSERT_NE(subscriber, nullptr);
  EXPECT_TRUE(subscriber->empty());
  subscriber->shutdown();
}
