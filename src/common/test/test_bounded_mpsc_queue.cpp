#include "common/bounded_mpsc_queue.hpp"

#include <gtest/gtest.h>

#include <utility>

TEST(BoundedMpscQueueTest, RejectsNewValueAndPreservesFifoAcrossWraparound)
{
  common::bounded_mpsc_queue_c<common::uint32_t> queue(2U);

  common::uint32_t first = 1U;
  common::uint32_t second = 2U;
  common::uint32_t rejected = 3U;
  EXPECT_TRUE(queue.try_push(std::move(first)));
  EXPECT_TRUE(queue.try_push(std::move(second)));
  EXPECT_FALSE(queue.try_push(std::move(rejected)));
  EXPECT_EQ(queue.get_dropped_count(), 1U);

  common::uint32_t output = 0U;
  ASSERT_TRUE(queue.try_pop(output));
  EXPECT_EQ(output, 1U);

  common::uint32_t wrapped = 4U;
  EXPECT_TRUE(queue.try_push(std::move(wrapped)));
  ASSERT_TRUE(queue.try_pop(output));
  EXPECT_EQ(output, 2U);
  ASSERT_TRUE(queue.try_pop(output));
  EXPECT_EQ(output, 4U);
  EXPECT_FALSE(queue.try_pop(output));
}

TEST(BoundedMpscQueueTest, DrainsAcceptedValuesAfterShutdown)
{
  common::bounded_mpsc_queue_c<common::uint32_t> queue(1U);
  common::uint32_t input = 7U;
  ASSERT_TRUE(queue.try_push(std::move(input)));

  queue.shutdown();

  common::uint32_t output = 0U;
  ASSERT_TRUE(queue.wait_and_pop(output));
  EXPECT_EQ(output, 7U);
  EXPECT_FALSE(queue.wait_and_pop(output));
}
