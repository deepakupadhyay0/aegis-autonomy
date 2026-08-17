#pragma once

#include "common/numeric_types.hpp"
#include "common/wraparound.hpp"

#include <limits>

namespace common
{
namespace time
{

inline constexpr int64_t NANOSECONDS_PER_MICROSECOND = 1000LL;
inline constexpr int64_t NANOSECONDS_PER_MILLISECOND = 1000000LL;
inline constexpr int64_t NANOSECONDS_PER_SECOND = 1000000000LL;
inline constexpr int64_t MICROSECONDS_PER_SECOND = 1000000LL;
inline constexpr int64_t MILLISECONDS_PER_SECOND = 1000LL;

inline constexpr int64_t INVALID_TIME_TICKS =
  std::numeric_limits<int64_t>::min();
inline constexpr int64_t MIN_TIME_TICKS = INVALID_TIME_TICKS + 1LL;
inline constexpr int64_t MAX_TIME_TICKS =
  std::numeric_limits<int64_t>::max();

enum class clock_e : uint8_t
{
  system = 0U,
  steady,
  ros
};

namespace detail
{

constexpr bool is_valid_ticks(const int64_t ticks) noexcept
{
  return ticks != INVALID_TIME_TICKS;
}

constexpr int64_t saturating_add_ticks(
  const int64_t left,
  const int64_t right) noexcept
{
  if (!is_valid_ticks(left) || !is_valid_ticks(right)) {
    return INVALID_TIME_TICKS;
  }

  if (common::add::will_overflow(left, right)) {
    return MAX_TIME_TICKS;
  }
  if (common::add::will_underflow(left, right)) {
    return MIN_TIME_TICKS;
  }
  const int64_t result = left + right;
  return result == INVALID_TIME_TICKS ? MIN_TIME_TICKS : result;
}

constexpr int64_t saturating_subtract_ticks(
  const int64_t left,
  const int64_t right) noexcept
{
  if (!is_valid_ticks(left) || !is_valid_ticks(right)) {
    return INVALID_TIME_TICKS;
  }

  if (common::sub::will_underflow(left, right)) {
    return MIN_TIME_TICKS;
  }
  if (common::sub::will_overflow(left, right)) {
    return MAX_TIME_TICKS;
  }
  const int64_t result = left - right;
  return result == INVALID_TIME_TICKS ? MIN_TIME_TICKS : result;
}

constexpr int64_t saturating_multiply_ticks(
  const int64_t left,
  const int64_t right) noexcept
{
  if (!is_valid_ticks(left)) {
    return INVALID_TIME_TICKS;
  }
  if (common::mul::will_overflow(left, right)) {
    return MAX_TIME_TICKS;
  }
  if (common::mul::will_underflow(left, right)) {
    return MIN_TIME_TICKS;
  }
  const int64_t result = left * right;
  return result == INVALID_TIME_TICKS ? MIN_TIME_TICKS : result;
}

constexpr int64_t ticks_from_seconds_nanoseconds(
  const int64_t seconds,
  const int64_t nanoseconds) noexcept
{
  int64_t seconds_adjustment = nanoseconds / NANOSECONDS_PER_SECOND;
  int64_t nanoseconds_remainder = nanoseconds % NANOSECONDS_PER_SECOND;
  if (nanoseconds_remainder < 0LL) {
    nanoseconds_remainder += NANOSECONDS_PER_SECOND;
    --seconds_adjustment;
  }

  if (common::add::will_overflow(seconds, seconds_adjustment)) {
    return MAX_TIME_TICKS;
  }
  if (common::add::will_underflow(seconds, seconds_adjustment)) {
    return MIN_TIME_TICKS;
  }

  const int64_t normalized_seconds = seconds + seconds_adjustment;
  constexpr int64_t maximum_seconds =
    MAX_TIME_TICKS / NANOSECONDS_PER_SECOND;
  constexpr int64_t maximum_remainder =
    MAX_TIME_TICKS % NANOSECONDS_PER_SECOND;
  constexpr int64_t minimum_seconds = -maximum_seconds - 1LL;
  constexpr int64_t minimum_remainder =
    MIN_TIME_TICKS - (minimum_seconds + 1LL) * NANOSECONDS_PER_SECOND +
    NANOSECONDS_PER_SECOND;

  if (normalized_seconds > maximum_seconds ||
    (normalized_seconds == maximum_seconds &&
    nanoseconds_remainder > maximum_remainder))
  {
    return MAX_TIME_TICKS;
  }
  if (normalized_seconds < minimum_seconds ||
    (normalized_seconds == minimum_seconds &&
    nanoseconds_remainder < minimum_remainder))
  {
    return MIN_TIME_TICKS;
  }

  if (normalized_seconds == minimum_seconds) {
    const int64_t remainder_offset = nanoseconds_remainder - minimum_remainder;
    return MIN_TIME_TICKS + remainder_offset;
  }

  if (common::mul::will_overflow(normalized_seconds, NANOSECONDS_PER_SECOND)) {
    return MAX_TIME_TICKS;
  }
  if (common::mul::will_underflow(normalized_seconds, NANOSECONDS_PER_SECOND)) {
    return MIN_TIME_TICKS;
  }
  const int64_t seconds_ticks = normalized_seconds * NANOSECONDS_PER_SECOND;
  if (common::add::will_overflow(seconds_ticks, nanoseconds_remainder)) {
    return MAX_TIME_TICKS;
  }
  if (common::add::will_underflow(seconds_ticks, nanoseconds_remainder)) {
    return MIN_TIME_TICKS;
  }
  const int64_t result = seconds_ticks + nanoseconds_remainder;
  return result == INVALID_TIME_TICKS ? MIN_TIME_TICKS : result;
}

}  // namespace detail

/// @brief Signed nanosecond duration with deterministic saturating arithmetic.
/// @details INT64_MIN is reserved as invalid. Invalid operands propagate and
/// arithmetic overflow saturates to min() or max(); operations never wrap.
class duration_c final
{
public:
  constexpr duration_c() noexcept = default;

  static constexpr duration_c invalid() noexcept
  {
    return duration_c(INVALID_TIME_TICKS);
  }

  static constexpr duration_c zero() noexcept
  {
    return duration_c(0LL);
  }

  static constexpr duration_c min() noexcept
  {
    return duration_c(MIN_TIME_TICKS);
  }

  static constexpr duration_c max() noexcept
  {
    return duration_c(MAX_TIME_TICKS);
  }

  static constexpr duration_c from_nanoseconds(const int64_t nanoseconds) noexcept
  {
    return duration_c(nanoseconds);
  }

  static constexpr duration_c from_microseconds(const int64_t microseconds) noexcept
  {
    return from_seconds_nanoseconds(
      microseconds / MICROSECONDS_PER_SECOND,
      (microseconds % MICROSECONDS_PER_SECOND) * NANOSECONDS_PER_MICROSECOND);
  }

  static constexpr duration_c from_milliseconds(const int64_t milliseconds) noexcept
  {
    return from_seconds_nanoseconds(
      milliseconds / MILLISECONDS_PER_SECOND,
      (milliseconds % MILLISECONDS_PER_SECOND) * NANOSECONDS_PER_MILLISECOND);
  }

  static constexpr duration_c from_seconds(const int64_t seconds) noexcept
  {
    return from_seconds_nanoseconds(seconds, 0LL);
  }

  static constexpr duration_c from_seconds_nanoseconds(
    const int64_t seconds,
    const int64_t nanoseconds) noexcept
  {
    return duration_c(detail::ticks_from_seconds_nanoseconds(seconds, nanoseconds));
  }

  constexpr bool is_valid() const noexcept
  {
    return detail::is_valid_ticks(m_ticks);
  }

  constexpr int64_t nanoseconds() const noexcept
  {
    return m_ticks;
  }

  constexpr int64_t microseconds() const noexcept
  {
    return is_valid() ? m_ticks / NANOSECONDS_PER_MICROSECOND : INVALID_TIME_TICKS;
  }

  constexpr int64_t milliseconds() const noexcept
  {
    return is_valid() ? m_ticks / NANOSECONDS_PER_MILLISECOND : INVALID_TIME_TICKS;
  }

  constexpr int64_t seconds() const noexcept
  {
    return is_valid() ? m_ticks / NANOSECONDS_PER_SECOND : INVALID_TIME_TICKS;
  }

  constexpr duration_c & operator+=(const duration_c right) noexcept
  {
    m_ticks = detail::saturating_add_ticks(m_ticks, right.m_ticks);
    return *this;
  }

  constexpr duration_c & operator-=(const duration_c right) noexcept
  {
    m_ticks = detail::saturating_subtract_ticks(m_ticks, right.m_ticks);
    return *this;
  }

  constexpr duration_c & operator*=(const int64_t right) noexcept
  {
    m_ticks = detail::saturating_multiply_ticks(m_ticks, right);
    return *this;
  }

  constexpr duration_c & operator/=(const int64_t right) noexcept
  {
    if (!is_valid() || right == 0LL) {
      m_ticks = INVALID_TIME_TICKS;
    } else {
      m_ticks /= right;
    }
    return *this;
  }

  constexpr duration_c operator+() const noexcept
  {
    return *this;
  }

  constexpr duration_c operator-() const noexcept
  {
    if (!is_valid()) {
      return invalid();
    }
    return duration_c(-m_ticks);
  }

  friend constexpr duration_c operator+(
    duration_c left,
    const duration_c right) noexcept
  {
    left += right;
    return left;
  }

  friend constexpr duration_c operator-(
    duration_c left,
    const duration_c right) noexcept
  {
    left -= right;
    return left;
  }

  friend constexpr duration_c operator*(
    duration_c left,
    const int64_t right) noexcept
  {
    left *= right;
    return left;
  }

  friend constexpr duration_c operator/(
    duration_c left,
    const int64_t right) noexcept
  {
    left /= right;
    return left;
  }

  friend constexpr bool operator==(
    const duration_c & left,
    const duration_c & right) noexcept = default;

  friend constexpr bool operator<(
    const duration_c left,
    const duration_c right) noexcept
  {
    return left.m_ticks < right.m_ticks;
  }

  friend constexpr bool operator<=(
    const duration_c left,
    const duration_c right) noexcept
  {
    return left.m_ticks <= right.m_ticks;
  }

  friend constexpr bool operator>(
    const duration_c left,
    const duration_c right) noexcept
  {
    return left.m_ticks > right.m_ticks;
  }

  friend constexpr bool operator>=(
    const duration_c left,
    const duration_c right) noexcept
  {
    return left.m_ticks >= right.m_ticks;
  }

private:
  constexpr explicit duration_c(const int64_t ticks) noexcept
  : m_ticks(ticks)
  {
  }

  int64_t m_ticks{0LL};
};

/// @brief Nanosecond time point whose clock domain is enforced at compile time.
/// @details Time arithmetic has the same invalid propagation and saturation
/// semantics as duration_c. Different clock domains cannot be compared or
/// subtracted accidentally.
template<clock_e clock_v>
class basic_time_c final
{
public:
  constexpr basic_time_c() noexcept = default;

  static constexpr clock_e clock() noexcept
  {
    return clock_v;
  }

  static constexpr basic_time_c invalid() noexcept
  {
    return basic_time_c(INVALID_TIME_TICKS);
  }

  static constexpr basic_time_c min() noexcept
  {
    return basic_time_c(MIN_TIME_TICKS);
  }

  static constexpr basic_time_c max() noexcept
  {
    return basic_time_c(MAX_TIME_TICKS);
  }

  static constexpr basic_time_c from_nanoseconds(const int64_t nanoseconds) noexcept
  {
    return basic_time_c(nanoseconds);
  }

  static constexpr basic_time_c from_seconds_nanoseconds(
    const int64_t seconds,
    const int64_t nanoseconds) noexcept
  {
    return basic_time_c(detail::ticks_from_seconds_nanoseconds(seconds, nanoseconds));
  }

  constexpr bool is_valid() const noexcept
  {
    return detail::is_valid_ticks(m_ticks);
  }

  constexpr int64_t nanoseconds() const noexcept
  {
    return m_ticks;
  }

  constexpr basic_time_c & operator+=(const duration_c duration) noexcept
  {
    m_ticks = detail::saturating_add_ticks(m_ticks, duration.nanoseconds());
    return *this;
  }

  constexpr basic_time_c & operator-=(const duration_c duration) noexcept
  {
    m_ticks = detail::saturating_subtract_ticks(m_ticks, duration.nanoseconds());
    return *this;
  }

  friend constexpr basic_time_c operator+(
    basic_time_c time,
    const duration_c duration) noexcept
  {
    time += duration;
    return time;
  }

  friend constexpr basic_time_c operator-(
    basic_time_c time,
    const duration_c duration) noexcept
  {
    time -= duration;
    return time;
  }

  friend constexpr duration_c operator-(
    const basic_time_c left,
    const basic_time_c right) noexcept
  {
    return duration_c::from_nanoseconds(
      detail::saturating_subtract_ticks(left.m_ticks, right.m_ticks));
  }

  friend constexpr bool operator==(
    const basic_time_c & left,
    const basic_time_c & right) noexcept = default;

  friend constexpr bool operator<(
    const basic_time_c left,
    const basic_time_c right) noexcept
  {
    return left.m_ticks < right.m_ticks;
  }

  friend constexpr bool operator<=(
    const basic_time_c left,
    const basic_time_c right) noexcept
  {
    return left.m_ticks <= right.m_ticks;
  }

  friend constexpr bool operator>(
    const basic_time_c left,
    const basic_time_c right) noexcept
  {
    return left.m_ticks > right.m_ticks;
  }

  friend constexpr bool operator>=(
    const basic_time_c left,
    const basic_time_c right) noexcept
  {
    return left.m_ticks >= right.m_ticks;
  }

private:
  constexpr explicit basic_time_c(const int64_t ticks) noexcept
  : m_ticks(ticks)
  {
  }

  int64_t m_ticks{INVALID_TIME_TICKS};
};

using system_time_c = basic_time_c<clock_e::system>;
using steady_time_c = basic_time_c<clock_e::steady>;
using ros_time_c = basic_time_c<clock_e::ros>;

/// @brief Reads CLOCK_REALTIME. Returns invalid when the native clock fails.
system_time_c system_now() noexcept;

/// @brief Reads CLOCK_MONOTONIC. Returns invalid when the native clock fails.
steady_time_c steady_now() noexcept;

static_assert(sizeof(duration_c) == sizeof(int64_t));
static_assert(sizeof(system_time_c) == sizeof(int64_t));
static_assert(sizeof(steady_time_c) == sizeof(int64_t));
static_assert(sizeof(ros_time_c) == sizeof(int64_t));

}  // namespace time
}  // namespace common
