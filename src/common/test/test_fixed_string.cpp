#include <gtest/gtest.h>

#include "common/fixed_string.hpp"

#include <string_view>

namespace
{

constexpr common::string8_t EXACT_CAPACITY_STRING{"12345678"};

static_assert(common::string8_t::capacity() == 8U);
static_assert(common::string16_t::capacity() == 16U);
static_assert(common::string32_t::capacity() == 32U);
static_assert(common::string64_t::capacity() == 64U);
static_assert(common::string128_t::capacity() == 128U);
static_assert(common::string256_t::capacity() == 256U);
static_assert(EXACT_CAPACITY_STRING.size() == 8U);
static_assert(EXACT_CAPACITY_STRING.view() == "12345678");
static_assert(EXACT_CAPACITY_STRING.c_str()[8U] == '\0');
static_assert(EXACT_CAPACITY_STRING == "12345678");
static_assert("12345678" == EXACT_CAPACITY_STRING);

}  // namespace

TEST(FixedStringTest, AcceptsExactCapacity)
{
  const common::string8_t value{"12345678"};

  EXPECT_EQ(value.size(), 8U);
  EXPECT_EQ(value.view(), "12345678");
  EXPECT_EQ(value.c_str()[value.size()], '\0');
}

TEST(FixedStringTest, ClipsRuntimeOverflow)
{
  common::string8_t value{"initial"};

  value.assign("123456789");

  EXPECT_EQ(value.view(), "12345678");
  EXPECT_EQ(value.c_str()[value.size()], '\0');
}

TEST(FixedStringTest, StopsAtEmbeddedRuntimeNull)
{
  common::string8_t value{"initial"};
  const char input[] = {'a', 'b', '\0', 'c'};

  value.assign(std::string_view(input, sizeof(input)));

  EXPECT_EQ(value.view(), "ab");
  EXPECT_EQ(value.c_str()[value.size()], '\0');
}

TEST(FixedStringTest, ClearsStorageAndLength)
{
  common::string8_t value{"value"};

  value.clear();

  EXPECT_TRUE(value.empty());
  EXPECT_EQ(value.size(), 0U);
  EXPECT_EQ(value.c_str()[0U], '\0');
}

TEST(FixedStringTest, SupportsAssignmentFromOwnView)
{
  common::string8_t value{"value"};

  value.assign(value.view());

  EXPECT_EQ(value.view(), "value");
}

TEST(FixedStringTest, AssignsFromStringView)
{
  common::string8_t value;
  const std::string_view source{"value"};

  value = source;

  EXPECT_EQ(value.view(), source);
}

TEST(FixedStringTest, ComparesWithStringViewsInBothOrders)
{
  const common::string8_t value{"value"};
  const std::string_view view{"value"};

  EXPECT_TRUE(value == view);
  EXPECT_TRUE(view == value);
}
