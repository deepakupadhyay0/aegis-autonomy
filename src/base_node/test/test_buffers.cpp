#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include "base_node/concurrent_ring_buffer.hpp"

TEST(ConcurrentRingBufferTest, BasicPushPop)
{
  base_node::topic::concurrent_ring_buffer_c<int> buffer(3);
  EXPECT_TRUE(buffer.empty());
  EXPECT_EQ(buffer.size(), 0U);
  EXPECT_EQ(buffer.capacity(), 3U);

  EXPECT_TRUE(buffer.push_back(10));
  EXPECT_TRUE(buffer.push_back(20));
  EXPECT_EQ(buffer.size(), 2U);
  EXPECT_FALSE(buffer.empty());

  auto val1 = buffer.pop_front();
  ASSERT_TRUE(val1.has_value());
  EXPECT_EQ(val1.value(), 10);

  auto val2 = buffer.pop_front();
  ASSERT_TRUE(val2.has_value());
  EXPECT_EQ(val2.value(), 20);

  EXPECT_TRUE(buffer.empty());
}

TEST(ConcurrentRingBufferTest, OverflowHandling)
{
  base_node::topic::concurrent_ring_buffer_c<int> buffer(2);
  EXPECT_TRUE(buffer.push_back(1));
  EXPECT_TRUE(buffer.push_back(2));
  EXPECT_EQ(buffer.size(), 2U);

  // Pushing when full should either overwrite or drop depending on ring_buffer implementation
  // Let's verify size remains bounded by capacity
  buffer.push_back(3);
  EXPECT_LE(buffer.size(), 2U);
}

TEST(ConcurrentRingBufferTest, MultithreadedWaitAndPop)
{
  base_node::topic::concurrent_ring_buffer_c<int> buffer(5);
  std::atomic<bool> running{true};
  std::vector<int> consumed_values;

  std::thread consumer([&]() {
    while (running.load()) {
      auto val = buffer.wait_and_pop_front_timeout(
        std::chrono::milliseconds(50), running);
      if (val.has_value()) {
        consumed_values.push_back(val.value());
      }
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  buffer.push_back(100);
  buffer.push_back(200);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  running = false;
  buffer.shutdown();
  if (consumer.joinable()) {
    consumer.join();
  }

  ASSERT_EQ(consumed_values.size(), 2U);
  EXPECT_EQ(consumed_values[0], 100);
  EXPECT_EQ(consumed_values[1], 200);
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
