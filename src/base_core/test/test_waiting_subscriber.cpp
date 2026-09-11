#include <gtest/gtest.h>

#include "base_core/create_waiting_subscriber.hpp"

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <std_msgs/msg/string.hpp>

#include <chrono>
#include <concepts>
#include <cstddef>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace
{

using message_t = std_msgs::msg::String;
using waiting_subscriber_t =
  base_core::topic::waiting_subscriber_c<message_t>;

static_assert(
  std::same_as<
    decltype(
      base_core::topic::create_waiting_subscriber<message_t>(
        std::declval<rclcpp::Node &>(),
        std::declval<const common::string256_t &>(),
        std::declval<const rclcpp::QoS &>())),
    std::shared_ptr<waiting_subscriber_t>>);

static_assert(
  std::same_as<
    decltype(
      base_core::topic::create_waiting_subscriber<message_t>(
        std::declval<rclcpp_lifecycle::LifecycleNode &>(),
        std::declval<const common::string256_t &>(),
        std::declval<const rclcpp::QoS &>())),
    std::shared_ptr<waiting_subscriber_t>>);

static_assert(
  std::same_as<
    decltype(std::declval<const waiting_subscriber_t &>().get_message()),
    message_t::ConstSharedPtr>);

static_assert(
  std::same_as<
    decltype(std::declval<const waiting_subscriber_t &>().get_message_info()),
    rmw_message_info_t>);

bool8_t wait_for_subscription(
  const rclcpp::Publisher<message_t>::SharedPtr & publisher,
  const std::size_t expected_count = 1U)
{
  const std::chrono::steady_clock::time_point deadline =
    std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    if (publisher->get_subscription_count() >= expected_count) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

template<typename node_t>
void check_synchronous_reception(node_t & node, const common::string256_t & topic_name)
{
  std::shared_ptr<waiting_subscriber_t> subscriber =
    base_core::topic::create_waiting_subscriber<message_t>(node, topic_name, rclcpp::QoS(1U));
  ASSERT_NE(subscriber, nullptr);
  EXPECT_FALSE(subscriber->has_new_message());
  EXPECT_EQ(subscriber->get_message(), nullptr);
  EXPECT_EQ(subscriber->wait_for_message(std::chrono::milliseconds(0)), RCL_RET_TIMEOUT);

  const rclcpp::Node::SharedPtr publisher_node =
    std::make_shared<rclcpp::Node>("waiting_subscriber_test_publisher");
  const rclcpp::Publisher<message_t>::SharedPtr publisher =
    publisher_node->create_publisher<message_t>(std::string(topic_name.view()), rclcpp::QoS(1U));
  ASSERT_TRUE(wait_for_subscription(publisher));

  message_t outgoing;
  outgoing.data = "first";
  publisher->publish(outgoing);
  ASSERT_EQ(subscriber->wait_for_message(std::chrono::seconds(5)), RCL_RET_OK);
  ASSERT_TRUE(subscriber->has_new_message());
  const message_t::ConstSharedPtr first = subscriber->get_message();
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first->data, "first");
  EXPECT_FALSE(subscriber->get_message_info().from_intra_process);

  outgoing.data = "second";
  publisher->publish(outgoing);
  ASSERT_EQ(subscriber->wait_for_message(std::chrono::seconds(5)), RCL_RET_OK);
  ASSERT_NE(subscriber->get_message(), nullptr);
  EXPECT_NE(subscriber->get_message(), first);
  EXPECT_EQ(subscriber->get_message()->data, "second");
  EXPECT_EQ(first->data, "first");

  EXPECT_EQ(subscriber->wait_for_message(std::chrono::milliseconds(10)), RCL_RET_TIMEOUT);
  EXPECT_FALSE(subscriber->has_new_message());
  EXPECT_EQ(subscriber->get_message(), nullptr);
  EXPECT_EQ(subscriber->shutdown(), RCL_RET_OK);
  EXPECT_EQ(subscriber->shutdown(), RCL_RET_OK);
  EXPECT_EQ(
    subscriber->wait_for_message(std::chrono::milliseconds(0)), RCL_RET_ALREADY_SHUTDOWN);
  subscriber.reset();
  EXPECT_EQ(first->data, "first");
}

template<typename node_t>
void check_intra_process_reception(
  node_t & node,
  const common::string256_t & topic_name,
  const rclcpp::IntraProcessSetting setting = rclcpp::IntraProcessSetting::NodeDefault)
{
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node.get_node_base_interface());
  std::shared_ptr<waiting_subscriber_t> subscriber =
    base_core::topic::create_waiting_subscriber<message_t>(
    node, topic_name, rclcpp::QoS(2U), setting);
  const rclcpp::NodeOptions publisher_options =
    rclcpp::NodeOptions().use_intra_process_comms(true);
  const rclcpp::Node::SharedPtr publisher_node = std::make_shared<rclcpp::Node>(
    "waiting_subscriber_intra_publisher", publisher_options);
  const rclcpp::Publisher<message_t>::SharedPtr publisher =
    publisher_node->create_publisher<message_t>(std::string(topic_name.view()), rclcpp::QoS(2U));
  ASSERT_EQ(publisher->get_intra_process_subscription_count(), 1U);

  std::unique_ptr<message_t> outgoing = std::make_unique<message_t>();
  outgoing->data = "first";
  // Borrowed address used only to verify that delivery preserves the message object.
  const message_t * const published_address = outgoing.get();
  publisher->publish(std::move(outgoing));
  executor.spin_some();
  EXPECT_FALSE(subscriber->has_new_message());
  ASSERT_EQ(subscriber->wait_for_message(std::chrono::seconds(5)), RCL_RET_OK);
  const message_t::ConstSharedPtr first = subscriber->get_message();
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first.get(), published_address);
  EXPECT_EQ(first->data, "first");
  const rmw_message_info_t first_info = subscriber->get_message_info();
  EXPECT_TRUE(first_info.from_intra_process);
  EXPECT_EQ(first_info.source_timestamp, 0);
  EXPECT_EQ(first_info.received_timestamp, 0);

  std::unique_ptr<message_t> next = std::make_unique<message_t>();
  next->data = "second";
  publisher->publish(std::move(next));
  ASSERT_EQ(subscriber->wait_for_message(std::chrono::seconds(5)), RCL_RET_OK);
  ASSERT_NE(subscriber->get_message(), nullptr);
  EXPECT_EQ(subscriber->get_message()->data, "second");
  EXPECT_EQ(first->data, "first");
  EXPECT_NE(subscriber->get_message(), first);
  EXPECT_EQ(subscriber->wait_for_message(std::chrono::milliseconds(20)), RCL_RET_TIMEOUT);
  EXPECT_EQ(subscriber->get_message(), nullptr);
  EXPECT_FALSE(subscriber->has_new_message());
  EXPECT_FALSE(subscriber->get_message_info().from_intra_process);
  EXPECT_TRUE(first_info.from_intra_process);
  EXPECT_EQ(subscriber->shutdown(), RCL_RET_OK);
  subscriber.reset();
  executor.remove_node(node.get_node_base_interface());
  EXPECT_EQ(first->data, "first");
}

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

TEST_F(waiting_subscriber_test_fixture_c, DdsMessageSurvivesLaterReceivesAndSubscriberDestruction)
{
  const rclcpp::Node::SharedPtr node =
    std::make_shared<rclcpp::Node>("waiting_subscriber_factory_test");
  check_synchronous_reception(
    *node,
    common::string256_t{"waiting_subscriber_test_topic"});
}

TEST_F(waiting_subscriber_test_fixture_c, ReceivesOnLifecycleNodeWithoutExecutor)
{
  const rclcpp_lifecycle::LifecycleNode::SharedPtr node =
    std::make_shared<rclcpp_lifecycle::LifecycleNode>(
    "lifecycle_waiting_subscriber_factory_test");
  check_synchronous_reception(
    *node,
    common::string256_t{"lifecycle_waiting_subscriber_test_topic"});
}

TEST_F(waiting_subscriber_test_fixture_c, ExplicitDisableUsesDdsAndExecutorCannotDrainSubscription)
{
  const rclcpp::NodeOptions node_options = rclcpp::NodeOptions().use_intra_process_comms(true);
  const rclcpp::Node::SharedPtr node =
    std::make_shared<rclcpp::Node>("waiting_subscriber_executor_test", node_options);
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);

  const common::string256_t topic_name{"waiting_subscriber_executor_topic"};
  const std::shared_ptr<waiting_subscriber_t> subscriber =
    base_core::topic::create_waiting_subscriber<message_t>(
    *node, topic_name, rclcpp::QoS(1U), rclcpp::IntraProcessSetting::Disable);
  const rclcpp::Publisher<message_t>::SharedPtr publisher =
    node->create_publisher<message_t>(std::string(topic_name.view()), rclcpp::QoS(1U));
  ASSERT_TRUE(wait_for_subscription(publisher));
  EXPECT_EQ(publisher->get_intra_process_subscription_count(), 0U);

  message_t outgoing;
  outgoing.data = "older";
  publisher->publish(outgoing);
  ASSERT_TRUE(publisher->wait_for_all_acked(std::chrono::seconds(5)));
  executor.spin_some();

  outgoing.data = "latest";
  publisher->publish(outgoing);
  ASSERT_TRUE(publisher->wait_for_all_acked(std::chrono::seconds(5)));
  executor.spin_some();

  ASSERT_EQ(subscriber->wait_for_message(std::chrono::seconds(5)), RCL_RET_OK);
  ASSERT_NE(subscriber->get_message(), nullptr);
  EXPECT_EQ(subscriber->get_message()->data, "latest");
  EXPECT_FALSE(subscriber->get_message_info().from_intra_process);
  EXPECT_EQ(subscriber->wait_for_message(std::chrono::milliseconds(10)), RCL_RET_TIMEOUT);
  EXPECT_FALSE(subscriber->has_new_message());
  EXPECT_EQ(subscriber->shutdown(), RCL_RET_OK);
  executor.remove_node(node);
}

TEST_F(waiting_subscriber_test_fixture_c, NodeDefaultPreservesIntraProcessPointerAndLifetime)
{
  const rclcpp::NodeOptions options = rclcpp::NodeOptions().use_intra_process_comms(true);
  const rclcpp::Node::SharedPtr node =
    std::make_shared<rclcpp::Node>("waiting_subscriber_intra_test", options);
  check_intra_process_reception(*node, common::string256_t{"waiting_subscriber_intra_topic"});
}

TEST_F(waiting_subscriber_test_fixture_c, ExplicitEnableOverridesDisabledNodeDefault)
{
  const rclcpp::Node::SharedPtr node =
    std::make_shared<rclcpp::Node>("waiting_subscriber_enable_test");
  check_intra_process_reception(
    *node, common::string256_t{"waiting_subscriber_enable_topic"},
    rclcpp::IntraProcessSetting::Enable);
}

TEST_F(waiting_subscriber_test_fixture_c, LifecycleNodeSupportsIntraProcessOwnership)
{
  const rclcpp::NodeOptions options = rclcpp::NodeOptions().use_intra_process_comms(true);
  const rclcpp_lifecycle::LifecycleNode::SharedPtr node =
    std::make_shared<rclcpp_lifecycle::LifecycleNode>(
    "waiting_subscriber_lifecycle_intra_test", options);
  check_intra_process_reception(
    *node, common::string256_t{"waiting_subscriber_lifecycle_intra_topic"});
}

TEST_F(waiting_subscriber_test_fixture_c, MixedSourcesAreDeliveredOnceAndRetainTheirMetadata)
{
  const rclcpp::NodeOptions options = rclcpp::NodeOptions().use_intra_process_comms(true);
  const rclcpp::Node::SharedPtr node =
    std::make_shared<rclcpp::Node>("waiting_subscriber_mixed_test", options);
  const common::string256_t topic_name{"waiting_subscriber_mixed_topic"};
  const std::shared_ptr<waiting_subscriber_t> subscriber =
    base_core::topic::create_waiting_subscriber<message_t>(*node, topic_name, rclcpp::QoS(10U));
  const rclcpp::Publisher<message_t>::SharedPtr intra_publisher =
    node->create_publisher<message_t>(std::string(topic_name.view()), rclcpp::QoS(10U));
  const rclcpp::Node::SharedPtr dds_node =
    std::make_shared<rclcpp::Node>("waiting_subscriber_dds_publisher");
  const rclcpp::Publisher<message_t>::SharedPtr dds_publisher =
    dds_node->create_publisher<message_t>(std::string(topic_name.view()), rclcpp::QoS(10U));
  // This DDS-only subscriber forces the local publisher to also publish through DDS.
  const std::shared_ptr<waiting_subscriber_t> dds_subscriber =
    base_core::topic::create_waiting_subscriber<message_t>(*dds_node, topic_name, rclcpp::QoS(10U));
  ASSERT_TRUE(wait_for_subscription(dds_publisher, 2U));
  ASSERT_TRUE(wait_for_subscription(intra_publisher, 2U));

  std::unique_ptr<message_t> local = std::make_unique<message_t>();
  local->data = "local";
  const message_t * const local_address = local.get();
  intra_publisher->publish(std::move(local));
  message_t remote;
  remote.data = "dds";
  dds_publisher->publish(remote);
  ASSERT_TRUE(intra_publisher->wait_for_all_acked(std::chrono::seconds(5)));
  ASSERT_TRUE(dds_publisher->wait_for_all_acked(std::chrono::seconds(5)));

  ASSERT_EQ(subscriber->wait_for_message(std::chrono::seconds(5)), RCL_RET_OK);
  const message_t::ConstSharedPtr first = subscriber->get_message();
  const rmw_message_info_t first_info = subscriber->get_message_info();
  ASSERT_EQ(subscriber->wait_for_message(std::chrono::seconds(5)), RCL_RET_OK);
  const message_t::ConstSharedPtr second = subscriber->get_message();
  const rmw_message_info_t second_info = subscriber->get_message_info();
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  EXPECT_NE(first->data, second->data);
  if (first->data == "local") {
    EXPECT_EQ(first.get(), local_address);
    EXPECT_TRUE(first_info.from_intra_process);
    EXPECT_EQ(second->data, "dds");
    EXPECT_FALSE(second_info.from_intra_process);
  } else {
    EXPECT_EQ(first->data, "dds");
    EXPECT_FALSE(first_info.from_intra_process);
    EXPECT_EQ(second.get(), local_address);
    EXPECT_EQ(second->data, "local");
    EXPECT_TRUE(second_info.from_intra_process);
  }
  EXPECT_EQ(subscriber->wait_for_message(std::chrono::milliseconds(50)), RCL_RET_TIMEOUT);
  EXPECT_EQ(subscriber->get_message(), nullptr);
  EXPECT_FALSE(subscriber->has_new_message());
  EXPECT_EQ(subscriber->shutdown(), RCL_RET_OK);
  EXPECT_EQ(dds_subscriber->shutdown(), RCL_RET_OK);
}

TEST_F(waiting_subscriber_test_fixture_c, ShutdownWakesReceiverAndPermanentlyStopsReception)
{
  const rclcpp::NodeOptions options = rclcpp::NodeOptions().use_intra_process_comms(true);
  const rclcpp::Node::SharedPtr node =
    std::make_shared<rclcpp::Node>("waiting_subscriber_shutdown_test", options);
  const std::shared_ptr<waiting_subscriber_t> subscriber =
    base_core::topic::create_waiting_subscriber<message_t>(
    *node, common::string256_t{"waiting_subscriber_shutdown_topic"}, rclcpp::QoS(1U));

  std::promise<void> started;
  std::future<void> started_future = started.get_future();
  std::future<rcl_ret_t> wait_result = std::async(
    std::launch::async,
    [subscriber, started = std::move(started)]() mutable {
      started.set_value();
      return subscriber->wait_for_message(std::chrono::seconds(5));
    });
  ASSERT_EQ(started_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
  EXPECT_EQ(wait_result.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);

  ASSERT_EQ(subscriber->shutdown(), RCL_RET_OK);
  EXPECT_EQ(subscriber->shutdown(), RCL_RET_OK);
  ASSERT_EQ(wait_result.wait_for(std::chrono::seconds(1)), std::future_status::ready);
  EXPECT_EQ(wait_result.get(), RCL_RET_ALREADY_SHUTDOWN);
  EXPECT_EQ(
    subscriber->wait_for_message(std::chrono::milliseconds(0)), RCL_RET_ALREADY_SHUTDOWN);
}
