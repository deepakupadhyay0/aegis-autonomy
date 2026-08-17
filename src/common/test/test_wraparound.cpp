#include "common/wraparound.hpp"

#include "common/numeric_types.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace
{

using common::int64_t;

constexpr int64_t MINIMUM = std::numeric_limits<int64_t>::min();
constexpr int64_t MAXIMUM = std::numeric_limits<int64_t>::max();
constexpr int64_t ZERO{0};
constexpr int64_t ONE{1};
constexpr int64_t TWO{2};
constexpr int64_t THREE{3};

static_assert(common::add::will_overflow(MAXIMUM, ONE));
static_assert(common::add::will_underflow(MINIMUM, -ONE));
static_assert(common::sub::will_overflow(MAXIMUM, -ONE));
static_assert(common::sub::will_underflow(MINIMUM, ONE));
static_assert(common::mul::will_overflow(MAXIMUM, TWO));
static_assert(common::mul::will_overflow(MINIMUM, -ONE));
static_assert(common::mul::will_underflow(MINIMUM, TWO));

TEST(WraparoundTest, ChecksAdditionBoundaries)
{
  EXPECT_FALSE(common::add::will_wrap(MAXIMUM, ZERO));
  EXPECT_FALSE(common::add::will_wrap(MAXIMUM - ONE, ONE));
  EXPECT_TRUE(common::add::will_overflow(MAXIMUM, ONE));
  EXPECT_TRUE(common::add::will_underflow(MINIMUM, -ONE));
}

TEST(WraparoundTest, ChecksSubtractionBoundaries)
{
  EXPECT_FALSE(common::sub::will_wrap(MINIMUM + ONE, ONE));
  EXPECT_FALSE(common::sub::will_wrap(MAXIMUM - ONE, -ONE));
  EXPECT_TRUE(common::sub::will_overflow(MAXIMUM, -ONE));
  EXPECT_TRUE(common::sub::will_underflow(MINIMUM, ONE));
}

TEST(WraparoundTest, ChecksEveryMultiplicationSignCombination)
{
  EXPECT_FALSE(common::mul::will_wrap(ZERO, MINIMUM));
  EXPECT_FALSE(common::mul::will_wrap(TWO, THREE));
  EXPECT_FALSE(common::mul::will_wrap(-TWO, THREE));
  EXPECT_FALSE(common::mul::will_wrap(TWO, -THREE));
  EXPECT_FALSE(common::mul::will_wrap(-TWO, -THREE));

  EXPECT_TRUE(common::mul::will_overflow(MAXIMUM, TWO));
  EXPECT_TRUE(common::mul::will_overflow(MINIMUM, -ONE));
  EXPECT_TRUE(common::mul::will_underflow(MINIMUM, TWO));
  EXPECT_TRUE(common::mul::will_underflow(MAXIMUM, -TWO));
}

TEST(WraparoundTest, ChecksUnsignedMultiplication)
{
  using uint64_t = common::uint64_t;
  constexpr uint64_t maximum = std::numeric_limits<uint64_t>::max();

  EXPECT_FALSE(common::mul::will_wrap(maximum, static_cast<uint64_t>(1U)));
  EXPECT_TRUE(common::mul::will_overflow(maximum, static_cast<uint64_t>(2U)));
  EXPECT_FALSE(common::mul::will_underflow(maximum, static_cast<uint64_t>(2U)));
}

}  // namespace
