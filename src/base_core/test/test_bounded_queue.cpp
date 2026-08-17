#include "base_core/bounded_queue.hpp"
#include "common/numeric_types.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <utility>

TEST(BoundedQueueTest, RejectsNewValueAndPreservesFifoAcrossWraparound)
{
  base_core::bounded_queue_c<common::uint32_t> queue(2U);

  EXPECT_TRUE(queue.try_push(1U));
  EXPECT_TRUE(queue.try_push(2U));
  EXPECT_FALSE(queue.try_push(3U));

  common::uint32_t output = 0U;
  ASSERT_TRUE(queue.try_pop(output));
  EXPECT_EQ(output, 1U);

  EXPECT_TRUE(queue.try_push(4U));
  ASSERT_TRUE(queue.try_pop(output));
  EXPECT_EQ(output, 2U);
  ASSERT_TRUE(queue.try_pop(output));
  EXPECT_EQ(output, 4U);
  EXPECT_FALSE(queue.try_pop(output));
}

TEST(BoundedQueueTest, ReleasesStoredResourcesWhenCleared)
{
  base_core::bounded_queue_c<
    std::shared_ptr<common::uint32_t>> queue(1U);
  std::shared_ptr<common::uint32_t> value =
    std::make_shared<common::uint32_t>(7U);
  const std::weak_ptr<common::uint32_t> observer(value);

  ASSERT_TRUE(queue.try_push(std::move(value)));
  EXPECT_FALSE(observer.expired());

  queue.clear();

  EXPECT_TRUE(queue.empty());
  EXPECT_TRUE(observer.expired());
}

TEST(BoundedQueueTest, RejectsZeroCapacity)
{
  EXPECT_THROW(
    base_core::bounded_queue_c<common::uint32_t>(0U),
    std::invalid_argument);
}
