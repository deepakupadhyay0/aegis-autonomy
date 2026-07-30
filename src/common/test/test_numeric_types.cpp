#include <gtest/gtest.h>

#include "common/numeric_types.hpp"

#include <cstdint>
#include <limits>
#include <type_traits>

static_assert(std::is_same_v<common::int8_t, std::int8_t>);
static_assert(std::is_same_v<common::int16_t, std::int16_t>);
static_assert(std::is_same_v<common::int32_t, std::int32_t>);
static_assert(std::is_same_v<common::int64_t, std::int64_t>);

static_assert(std::is_same_v<common::uint8_t, std::uint8_t>);
static_assert(std::is_same_v<common::uint16_t, std::uint16_t>);
static_assert(std::is_same_v<common::uint32_t, std::uint32_t>);
static_assert(std::is_same_v<common::uint64_t, std::uint64_t>);

TEST(NumericTypesTest, UsesIeee754FloatingPoint)
{
  EXPECT_TRUE(std::numeric_limits<common::float32_t>::is_iec559);
  EXPECT_TRUE(std::numeric_limits<common::float64_t>::is_iec559);
  EXPECT_EQ(sizeof(common::float32_t), 4U);
  EXPECT_EQ(sizeof(common::float64_t), 8U);
}
