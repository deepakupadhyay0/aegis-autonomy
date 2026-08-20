#include <gtest/gtest.h>

#include "common/strict_vector.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>

static_assert(
  std::ranges::contiguous_range<common::strict_vector_c<std::int32_t>>);
static_assert(
  std::ranges::sized_range<common::strict_vector_c<std::int32_t>>);

TEST(StrictVectorTest, AcceptsExactCapacityAndRejectsOverflow)
{
  common::strict_vector_c<std::int32_t> values{3U};

  EXPECT_TRUE(values.try_push_back(10));
  EXPECT_TRUE(values.try_push_back(20));
  EXPECT_TRUE(values.try_push_back(30));
  EXPECT_TRUE(values.full());
  EXPECT_FALSE(values.try_push_back(40));
  EXPECT_EQ(values.size(), 3U);
}

TEST(StrictVectorTest, ReusesCapacityAfterPopAndClear)
{
  common::strict_vector_c<std::int32_t> values{2U};
  ASSERT_TRUE(values.try_push_back(10));
  ASSERT_TRUE(values.try_push_back(20));

  std::int32_t removed = 0;
  EXPECT_TRUE(values.try_pop_back(removed));
  EXPECT_EQ(removed, 20);
  EXPECT_TRUE(values.try_push_back(30));

  values.clear();
  EXPECT_TRUE(values.empty());
  EXPECT_TRUE(values.try_push_back(40));
  EXPECT_EQ(
    values.peak_memory_bytes(),
    2U * sizeof(std::int32_t));
}

TEST(StrictVectorTest, ProvidesCheckedAccessToActiveElements)
{
  common::strict_vector_c<std::int32_t> values{1U};
  ASSERT_TRUE(values.try_push_back(42));

  ASSERT_NE(values.try_at(0U), nullptr);
  EXPECT_EQ(*values.try_at(0U), 42);
  EXPECT_EQ(values.try_at(1U), nullptr);
}

TEST(StrictVectorTest, SupportsStandardAlgorithmsAndIteratorForms)
{
  common::strict_vector_c<std::int32_t> values{3U};
  ASSERT_TRUE(values.try_push_back(30));
  ASSERT_TRUE(values.try_push_back(10));
  ASSERT_TRUE(values.try_push_back(20));

  std::ranges::sort(values);

  EXPECT_EQ(values.front(), 10);
  EXPECT_EQ(values[1U], 20);
  EXPECT_EQ(values.back(), 30);
  EXPECT_EQ(*values.cbegin(), 10);
  EXPECT_EQ(*values.crbegin(), 30);
  EXPECT_EQ(values.data(), &values.front());
  EXPECT_EQ(values.max_size(), 3U);
}

TEST(StrictVectorTest, SupportsEraseRemoveAlgorithms)
{
  common::strict_vector_c<std::int32_t> values{4U};
  ASSERT_TRUE(values.try_push_back(10));
  ASSERT_TRUE(values.try_push_back(20));
  ASSERT_TRUE(values.try_push_back(10));
  ASSERT_TRUE(values.try_push_back(30));

  const common::strict_vector_c<std::int32_t>::iterator new_end =
    std::remove(values.begin(), values.end(), 10);
  values.erase(new_end, values.end());

  ASSERT_EQ(values.size(), 2U);
  EXPECT_EQ(values[0U], 20);
  EXPECT_EQ(values[1U], 30);
}

TEST(StrictVectorTest, GuardsResizeAssignAndInsert)
{
  common::strict_vector_c<std::int32_t> values{3U};
  EXPECT_TRUE(values.try_resize(2U, 10));
  EXPECT_FALSE(values.try_resize(4U));

  const std::array<std::int32_t, 3U> assigned{10, 20, 30};
  EXPECT_TRUE(values.try_assign(assigned));
  const std::array<std::int32_t, 4U> oversized{10, 20, 30, 40};
  EXPECT_FALSE(values.try_assign(oversized));

  values.pop_back();
  EXPECT_TRUE(values.try_insert(values.cbegin() + 1, 15));
  EXPECT_FALSE(values.try_insert(values.cend(), 40));
  EXPECT_EQ(values[0U], 10);
  EXPECT_EQ(values[1U], 15);
  EXPECT_EQ(values[2U], 20);
}

TEST(StrictVectorTest, SupportsMoveOnlyValues)
{
  using pointer_t = std::unique_ptr<std::int32_t>;
  common::strict_vector_c<pointer_t> values{1U};

  EXPECT_TRUE(values.try_push_back(std::make_unique<std::int32_t>(42)));
  ASSERT_NE(values.try_at(0U), nullptr);
  EXPECT_EQ(**values.try_at(0U), 42);
}

TEST(StrictVectorTest, BoundsSharedPointerSlots)
{
  using pointer_t = std::shared_ptr<std::int32_t>;
  common::strict_vector_c<pointer_t> values{2U};

  EXPECT_TRUE(values.try_push_back(std::make_shared<std::int32_t>(10)));
  EXPECT_TRUE(values.try_push_back(std::make_shared<std::int32_t>(20)));
  EXPECT_FALSE(values.try_push_back(std::make_shared<std::int32_t>(30)));

  pointer_t removed;
  EXPECT_TRUE(values.try_pop_back(removed));
  EXPECT_TRUE(values.try_push_back(std::make_shared<std::int32_t>(40)));
}

TEST(StrictVectorTest, OwnsAndReportsItsElementStorageBudget)
{
  common::strict_vector_c<std::int32_t> values{2U};

  EXPECT_TRUE(values.owns_memory_resource());
  EXPECT_EQ(
    values.memory_limit_bytes(),
    2U * sizeof(std::int32_t));
  EXPECT_EQ(
    values.current_memory_bytes(),
    2U * sizeof(std::int32_t));
  EXPECT_EQ(
    values.peak_memory_bytes(),
    2U * sizeof(std::int32_t));
  EXPECT_EQ(values.failed_allocations(), 0U);
}

TEST(StrictVectorTest, AcceptsBorrowedMemoryResource)
{
  alignas(std::int32_t)
  std::array<std::byte, 2U * sizeof(std::int32_t)> storage{};
  std::pmr::monotonic_buffer_resource resource{
    storage.data(),
    storage.size(),
    std::pmr::null_memory_resource()};
  common::strict_vector_c<std::int32_t> values{2U, resource};

  EXPECT_FALSE(values.owns_memory_resource());
  EXPECT_EQ(values.memory_limit_bytes(), std::nullopt);
  EXPECT_TRUE(values.try_push_back(10));
  EXPECT_TRUE(values.try_push_back(20));
  EXPECT_FALSE(values.try_push_back(30));
}

TEST(StrictVectorTest, RejectsZeroCapacity)
{
  EXPECT_THROW(
    common::strict_vector_c<std::int32_t>(0U),
    std::invalid_argument);
}
