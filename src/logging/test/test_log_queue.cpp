#include "logging/log_queue.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <utility>

TEST(LogQueueTest, RejectsNewRecordWithoutOverwritingQueuedRecords)
{
  logging::log_queue_c queue(2U);

  logging::log_record_s first;
  first.message = "first";
  logging::log_record_s second;
  second.message = "second";
  logging::log_record_s third;
  third.message = "third";

  EXPECT_TRUE(queue.try_push(std::move(first)));
  EXPECT_TRUE(queue.try_push(std::move(second)));
  EXPECT_FALSE(queue.try_push(std::move(third)));
  EXPECT_EQ(queue.get_dropped_record_count(), 1U);

  logging::log_record_s output;
  ASSERT_TRUE(queue.wait_and_pop(output));
  EXPECT_EQ(output.message, "first");
  ASSERT_TRUE(queue.wait_and_pop(output));
  EXPECT_EQ(output.message, "second");

  const logging::log_queue_statistics_s statistics = queue.get_statistics();
  EXPECT_EQ(statistics.capacity, 2U);
  EXPECT_EQ(statistics.accepted_record_count, 2U);
  EXPECT_EQ(statistics.dropped_capacity_count, 1U);
  EXPECT_EQ(statistics.peak_occupancy, 2U);
  EXPECT_EQ(statistics.pressure_event_count, 1U);
  EXPECT_TRUE(queue.consume_pressure_request());
  EXPECT_FALSE(queue.consume_pressure_request());

  queue.shutdown();
  EXPECT_FALSE(queue.wait_and_pop(output));
}

TEST(LogQueueTest, RequestsPressureFlushAtNinetyPercentCapacity)
{
  logging::log_queue_c queue(10U);
  for (uint32_t index = 0U; index < 8U; ++index) {
    logging::log_record_s record;
    EXPECT_TRUE(queue.try_push(std::move(record)));
  }
  EXPECT_FALSE(queue.consume_pressure_request());

  logging::log_record_s ninth_record;
  EXPECT_TRUE(queue.try_push(std::move(ninth_record)));
  EXPECT_TRUE(queue.consume_pressure_request());
}
