#include "common/time.hpp"
#include "common/time_conversion.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <limits>
#include <type_traits>

namespace
{

using common::time::duration_c;
using common::time::steady_time_c;
using common::time::system_time_c;

static_assert(sizeof(duration_c) == sizeof(common::int64_t));
static_assert(sizeof(system_time_c) == sizeof(common::int64_t));
static_assert(!std::is_same_v<system_time_c, steady_time_c>);
static_assert(
  (duration_c::max() + duration_c::from_nanoseconds(1LL)) ==
  duration_c::max());
static_assert(
  (duration_c::min() - duration_c::from_nanoseconds(1LL)) ==
  duration_c::min());

TEST(TimeTest, NormalizesSecondsAndNanoseconds)
{
  const system_time_c positive = system_time_c::from_seconds_nanoseconds(
    1LL,
    1500000000LL);
  EXPECT_EQ(positive.nanoseconds(), 2500000000LL);

  const system_time_c negative = system_time_c::from_seconds_nanoseconds(
    0LL,
    -1LL);
  EXPECT_EQ(negative.nanoseconds(), -1LL);

  const std::optional<common::time::seconds_nanoseconds_s> split =
    common::time::split_time(negative.nanoseconds());
  ASSERT_TRUE(split.has_value());
  EXPECT_EQ(split->seconds, -1LL);
  EXPECT_EQ(split->nanoseconds, 999999999);
}

TEST(TimeTest, SaturatesInsteadOfWrapping)
{
  const duration_c one = duration_c::from_nanoseconds(1LL);

  EXPECT_EQ((duration_c::max() + one).nanoseconds(), duration_c::max().nanoseconds());
  EXPECT_EQ((duration_c::min() - one).nanoseconds(), duration_c::min().nanoseconds());
  EXPECT_EQ((system_time_c::max() + one).nanoseconds(), system_time_c::max().nanoseconds());
  EXPECT_EQ((system_time_c::min() - one).nanoseconds(), system_time_c::min().nanoseconds());

  const duration_c multiplied = duration_c::from_seconds(1LL) *
    std::numeric_limits<common::int64_t>::max();
  EXPECT_EQ(multiplied, duration_c::max());

  const duration_c reserved_minimum_product =
    duration_c::from_nanoseconds(1LL) *
    std::numeric_limits<common::int64_t>::min();
  EXPECT_EQ(reserved_minimum_product, duration_c::min());
  EXPECT_TRUE(reserved_minimum_product.is_valid());
}

TEST(TimeTest, ChecksExactConstructionBoundaries)
{
  constexpr common::int64_t maximum_seconds = 9223372036LL;
  constexpr common::int64_t maximum_nanoseconds = 854775807LL;
  constexpr common::int64_t minimum_seconds = -9223372037LL;
  constexpr common::int64_t minimum_nanoseconds = 145224193LL;

  EXPECT_EQ(
    system_time_c::from_seconds_nanoseconds(
      maximum_seconds,
      maximum_nanoseconds),
    system_time_c::max());
  EXPECT_EQ(
    system_time_c::from_seconds_nanoseconds(
      maximum_seconds,
      maximum_nanoseconds + 1LL),
    system_time_c::max());
  EXPECT_EQ(
    system_time_c::from_seconds_nanoseconds(
      minimum_seconds,
      minimum_nanoseconds),
    system_time_c::min());
  EXPECT_EQ(
    system_time_c::from_seconds_nanoseconds(
      minimum_seconds,
      minimum_nanoseconds - 1LL),
    system_time_c::min());
}

TEST(TimeTest, PropagatesInvalidValues)
{
  const duration_c valid = duration_c::from_nanoseconds(10LL);

  EXPECT_FALSE((duration_c::invalid() + valid).is_valid());
  EXPECT_FALSE((system_time_c::invalid() + valid).is_valid());
  EXPECT_FALSE((valid / 0LL).is_valid());
}

TEST(TimeTest, SerializesClockDomainAndTicks)
{
  const system_time_c original = system_time_c::from_nanoseconds(-123456789LL);
  const common::string64_t serialized = common::time::serialize_time(original);
  EXPECT_EQ(serialized, std::string_view("system:-123456789"));

  const std::optional<system_time_c> parsed =
    common::time::deserialize_system_time(serialized.view());
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed.value(), original);

  EXPECT_FALSE(common::time::deserialize_steady_time(serialized.view()).has_value());
}

TEST(TimeTest, ConvertsNormalizedPosixTimespec)
{
  const system_time_c original = system_time_c::from_nanoseconds(-1LL);
  const std::optional<struct timespec> converted = common::time::to_timespec(original);
  ASSERT_TRUE(converted.has_value());
  EXPECT_EQ(converted->tv_sec, -1);
  EXPECT_EQ(
    converted->tv_nsec,
    static_cast<decltype(converted->tv_nsec)>(999999999));
  EXPECT_EQ(common::time::system_time_from_timespec(converted.value()), original);

  const struct timespec invalid_parts{0, -1};
  EXPECT_FALSE(common::time::system_time_from_timespec(invalid_parts).is_valid());
}

TEST(TimeTest, ConvertsChronoDurationsWithoutChangingUnits)
{
  const duration_c converted = common::time::from_chrono(std::chrono::milliseconds(25LL));
  EXPECT_EQ(converted.nanoseconds(), 25000000LL);

  const std::optional<std::chrono::nanoseconds> restored =
    common::time::to_chrono(converted);
  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(restored->count(), 25000000LL);
}

TEST(TimeTest, FormatsUtcWithoutLocalTimezone)
{
  const system_time_c unix_epoch = system_time_c::from_nanoseconds(0LL);
  EXPECT_EQ(
    common::time::format_utc(unix_epoch),
    std::string_view("1970-01-01T00:00:00.000000000Z"));
}

}  // namespace
