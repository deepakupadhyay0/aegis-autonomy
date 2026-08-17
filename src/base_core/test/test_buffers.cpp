#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include "base_core/concurrent_ring_buffer.hpp"

TEST(ConcurrentRingBufferTest, BasicPushPop)
{
  base_core::concurrent_ring_buffer_c<int32_t> buffer(3);
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
  base_core::concurrent_ring_buffer_c<int32_t> buffer(2);
  EXPECT_TRUE(buffer.push_back(1));
  EXPECT_TRUE(buffer.push_back(2));
  EXPECT_EQ(buffer.size(), 2U);

  EXPECT_TRUE(buffer.push_back(3));
  EXPECT_EQ(buffer.size(), 2U);

  std::optional<int32_t> first = buffer.pop_front();
  std::optional<int32_t> second = buffer.pop_front();
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(first.value(), 2);
  EXPECT_EQ(second.value(), 3);
}

TEST(ConcurrentRingBufferTest, MultithreadedWaitAndPop)
{
  base_core::concurrent_ring_buffer_c<int32_t> buffer(5);
  std::atomic<bool> running{true};
  std::vector<int32_t> consumed_values;

  std::thread consumer([&]() {
    while (running.load()) {
      auto val = buffer.wait_and_pop_front_timeout(std::chrono::milliseconds(50));
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

TEST(ConcurrentRingBufferTest, ReleasesConsumedResources)
{
  base_core::concurrent_ring_buffer_c<std::shared_ptr<int32_t>> buffer(1U);
  std::shared_ptr<int32_t> value = std::make_shared<int32_t>(42);
  std::weak_ptr<int32_t> observer = value;

  ASSERT_TRUE(buffer.push_back(std::move(value)));
  EXPECT_FALSE(value);

  std::optional<std::shared_ptr<int32_t>> popped = buffer.pop_front();
  ASSERT_TRUE(popped.has_value());
  EXPECT_FALSE(observer.expired());
  popped.reset();
  EXPECT_TRUE(observer.expired());
}

TEST(ConcurrentRingBufferTest, ClearReleasesQueuedResources)
{
  base_core::concurrent_ring_buffer_c<std::shared_ptr<int32_t>> buffer(1U);
  std::shared_ptr<int32_t> value = std::make_shared<int32_t>(42);
  std::weak_ptr<int32_t> observer = value;

  ASSERT_TRUE(buffer.push_back(std::move(value)));
  buffer.clear();
  EXPECT_TRUE(observer.expired());
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
