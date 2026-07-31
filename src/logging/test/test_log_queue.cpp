#include "logging/log_queue.hpp"

#include <gtest/gtest.h>

#include <utility>

TEST(LogQueueTest, OverwritesOldestRecord)
{
  logging::log_queue_c queue(2U);

  common::logging::log_record_s first;
  first.message = "first";
  common::logging::log_record_s second;
  second.message = "second";
  common::logging::log_record_s third;
  third.message = "third";

  EXPECT_TRUE(queue.try_push(std::move(first)));
  EXPECT_TRUE(queue.try_push(std::move(second)));
  EXPECT_TRUE(queue.try_push(std::move(third)));
  EXPECT_EQ(queue.get_dropped_record_count(), 1U);

  common::logging::log_record_s output;
  ASSERT_TRUE(queue.wait_and_pop(output));
  EXPECT_EQ(output.message, "second");
  ASSERT_TRUE(queue.wait_and_pop(output));
  EXPECT_EQ(output.message, "third");

  queue.shutdown();
  EXPECT_FALSE(queue.wait_and_pop(output));
}
