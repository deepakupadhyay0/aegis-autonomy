#pragma once

#include <climits>
#include <cstdint>
#include <limits>

namespace common
{

using bool8_t = bool;

using int8_t = std::int8_t;
using int16_t = std::int16_t;
using int32_t = std::int32_t;
using int64_t = std::int64_t;

using uint8_t = std::uint8_t;
using uint16_t = std::uint16_t;
using uint32_t = std::uint32_t;
using uint64_t = std::uint64_t;

using float32_t = float;
using float64_t = double;

static_assert(CHAR_BIT == 8, "The autonomy stack requires 8-bit bytes");

static_assert(sizeof(int8_t) * CHAR_BIT == 8U);
static_assert(sizeof(int16_t) * CHAR_BIT == 16U);
static_assert(sizeof(int32_t) * CHAR_BIT == 32U);
static_assert(sizeof(int64_t) * CHAR_BIT == 64U);

static_assert(sizeof(uint8_t) * CHAR_BIT == 8U);
static_assert(sizeof(uint16_t) * CHAR_BIT == 16U);
static_assert(sizeof(uint32_t) * CHAR_BIT == 32U);
static_assert(sizeof(uint64_t) * CHAR_BIT == 64U);

static_assert(sizeof(float32_t) * CHAR_BIT == 32U);
static_assert(std::numeric_limits<float32_t>::is_iec559);
static_assert(std::numeric_limits<float32_t>::radix == 2);
static_assert(std::numeric_limits<float32_t>::digits == 24);
static_assert(std::numeric_limits<float32_t>::max_exponent == 128);

static_assert(sizeof(float64_t) * CHAR_BIT == 64U);
static_assert(std::numeric_limits<float64_t>::is_iec559);
static_assert(std::numeric_limits<float64_t>::radix == 2);
static_assert(std::numeric_limits<float64_t>::digits == 53);
static_assert(std::numeric_limits<float64_t>::max_exponent == 1024);

}  // namespace common
