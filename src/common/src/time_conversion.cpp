#include "common/time_conversion.hpp"

#include <charconv>
#include <cstdio>
#include <ctime>
#include <limits>
#include <ratio>
#include <system_error>
#include <type_traits>

namespace common
{
namespace time
{
namespace
{

string64_t serialize_ticks(
  const clock_e clock,
  const int64_t ticks) noexcept
{
  string64_t serialized;
  char buffer[string64_t::capacity() + 1U]{};
  const std::string_view name = clock_name(clock);
  std::size_t size = 0U;
  for (const char character : name) {
    buffer[size] = character;
    ++size;
  }
  buffer[size] = ':';
  ++size;

  if (ticks == INVALID_TIME_TICKS) {
    constexpr std::string_view invalid_value{"invalid"};
    for (const char character : invalid_value) {
      buffer[size] = character;
      ++size;
    }
  } else {
    const std::to_chars_result result = std::to_chars(
      buffer + size,
      buffer + string64_t::capacity(),
      ticks);
    if (result.ec != std::errc{}) {
      return serialized;
    }
    size = static_cast<std::size_t>(result.ptr - buffer);
  }

  serialized = std::string_view(buffer, size);
  return serialized;
}

std::optional<int64_t> deserialize_ticks(
  const std::string_view serialized,
  const clock_e expected_clock) noexcept
{
  const std::string_view name = clock_name(expected_clock);
  if (serialized.size() <= name.size() ||
    serialized.substr(0U, name.size()) != name ||
    serialized[name.size()] != ':')
  {
    return std::nullopt;
  }

  const std::string_view value = serialized.substr(name.size() + 1U);
  if (value == "invalid") {
    return INVALID_TIME_TICKS;
  }
  if (value.empty()) {
    return std::nullopt;
  }

  int64_t ticks = 0LL;
  const std::from_chars_result result = std::from_chars(
    value.data(),
    value.data() + value.size(),
    ticks);
  if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
    return std::nullopt;
  }
  if (ticks == INVALID_TIME_TICKS) {
    return std::nullopt;
  }
  return ticks;
}

template<clock_e clock_v>
std::optional<basic_time_c<clock_v>> deserialize_time(
  const std::string_view serialized) noexcept
{
  const std::optional<int64_t> ticks = deserialize_ticks(serialized, clock_v);
  if (!ticks.has_value()) {
    return std::nullopt;
  }
  return basic_time_c<clock_v>::from_nanoseconds(ticks.value());
}

std::optional<struct timespec> ticks_to_timespec(const int64_t ticks) noexcept
{
  static_assert(std::numeric_limits<std::time_t>::is_signed);
  static_assert(sizeof(std::time_t) <= sizeof(int64_t));

  const std::optional<seconds_nanoseconds_s> parts = split_time(ticks);
  if (!parts.has_value() ||
    parts->seconds < static_cast<int64_t>(std::numeric_limits<std::time_t>::min()) ||
    parts->seconds > static_cast<int64_t>(std::numeric_limits<std::time_t>::max()))
  {
    return std::nullopt;
  }

  const struct timespec converted
  {
    static_cast<std::time_t>(parts->seconds),
    static_cast<decltype(timespec::tv_nsec)>(parts->nanoseconds)
  };
  return converted;
}

std::optional<int64_t> ticks_from_timespec(const struct timespec & time) noexcept
{
  if (time.tv_nsec < static_cast<decltype(time.tv_nsec)>(0) ||
    time.tv_nsec >= static_cast<decltype(timespec::tv_nsec)>(NANOSECONDS_PER_SECOND))
  {
    return std::nullopt;
  }
  return detail::ticks_from_seconds_nanoseconds(
    static_cast<int64_t>(time.tv_sec),
    static_cast<int64_t>(time.tv_nsec));
}

template<typename chrono_duration_t>
int64_t ticks_from_chrono_duration(const chrono_duration_t duration) noexcept
{
  using rep_t = typename chrono_duration_t::rep;
  using period_t = typename chrono_duration_t::period;
  static_assert(std::is_integral_v<rep_t>);
  static_assert(std::numeric_limits<rep_t>::is_signed);
  static_assert(sizeof(rep_t) <= sizeof(int64_t));
  static_assert(std::ratio_less_equal_v<period_t, std::chrono::seconds::period>);
  static_assert(std::ratio_less_equal_v<std::chrono::nanoseconds::period, period_t>);

  const std::chrono::seconds seconds =
    std::chrono::duration_cast<std::chrono::seconds>(duration);
  const chrono_duration_t remainder =
    duration - std::chrono::duration_cast<chrono_duration_t>(seconds);
  const std::chrono::nanoseconds nanoseconds =
    std::chrono::duration_cast<std::chrono::nanoseconds>(remainder);
  return detail::ticks_from_seconds_nanoseconds(
    static_cast<int64_t>(seconds.count()),
    nanoseconds.count());
}

template<typename chrono_duration_t>
chrono_duration_t chrono_duration_from_ticks(const int64_t ticks) noexcept
{
  using rep_t = typename chrono_duration_t::rep;
  using period_t = typename chrono_duration_t::period;
  static_assert(std::is_integral_v<rep_t>);
  static_assert(std::numeric_limits<rep_t>::is_signed);
  static_assert(sizeof(rep_t) >= sizeof(int64_t));
  static_assert(std::ratio_less_equal_v<period_t, std::chrono::seconds::period>);
  static_assert(std::ratio_less_equal_v<std::chrono::nanoseconds::period, period_t>);

  return std::chrono::duration_cast<chrono_duration_t>(
    std::chrono::nanoseconds(ticks));
}

}  // namespace

std::optional<seconds_nanoseconds_s> split_time(const int64_t ticks) noexcept
{
  if (ticks == INVALID_TIME_TICKS) {
    return std::nullopt;
  }

  int64_t seconds = ticks / NANOSECONDS_PER_SECOND;
  int64_t nanoseconds = ticks % NANOSECONDS_PER_SECOND;
  if (nanoseconds < 0LL) {
    nanoseconds += NANOSECONDS_PER_SECOND;
    --seconds;
  }
  return seconds_nanoseconds_s{
    seconds,
    static_cast<int32_t>(nanoseconds)};
}

std::optional<struct timespec> to_timespec(const duration_c duration) noexcept
{
  return ticks_to_timespec(duration.nanoseconds());
}

std::optional<struct timespec> to_timespec(const system_time_c time) noexcept
{
  return ticks_to_timespec(time.nanoseconds());
}

std::optional<struct timespec> to_timespec(const steady_time_c time) noexcept
{
  return ticks_to_timespec(time.nanoseconds());
}

duration_c duration_from_timespec(const struct timespec & time) noexcept
{
  const std::optional<int64_t> ticks = ticks_from_timespec(time);
  return ticks.has_value() ?
         duration_c::from_nanoseconds(ticks.value()) :
         duration_c::invalid();
}

system_time_c system_time_from_timespec(const struct timespec & time) noexcept
{
  const std::optional<int64_t> ticks = ticks_from_timespec(time);
  return ticks.has_value() ?
         system_time_c::from_nanoseconds(ticks.value()) :
         system_time_c::invalid();
}

steady_time_c steady_time_from_timespec(const struct timespec & time) noexcept
{
  const std::optional<int64_t> ticks = ticks_from_timespec(time);
  return ticks.has_value() ?
         steady_time_c::from_nanoseconds(ticks.value()) :
         steady_time_c::invalid();
}

std::optional<std::chrono::nanoseconds> to_chrono(
  const duration_c duration) noexcept
{
  if (!duration.is_valid()) {
    return std::nullopt;
  }
  return std::chrono::nanoseconds(duration.nanoseconds());
}

std::optional<std::chrono::system_clock::time_point> to_chrono(
  const system_time_c time) noexcept
{
  if (!time.is_valid()) {
    return std::nullopt;
  }
  return std::chrono::system_clock::time_point{
    chrono_duration_from_ticks<std::chrono::system_clock::duration>(
      time.nanoseconds())};
}

std::optional<std::chrono::steady_clock::time_point> to_chrono(
  const steady_time_c time) noexcept
{
  if (!time.is_valid()) {
    return std::nullopt;
  }
  return std::chrono::steady_clock::time_point{
    chrono_duration_from_ticks<std::chrono::steady_clock::duration>(
      time.nanoseconds())};
}

system_time_c from_chrono(
  const std::chrono::system_clock::time_point time) noexcept
{
  return system_time_c::from_nanoseconds(
    ticks_from_chrono_duration(time.time_since_epoch()));
}

steady_time_c from_chrono(
  const std::chrono::steady_clock::time_point time) noexcept
{
  return steady_time_c::from_nanoseconds(
    ticks_from_chrono_duration(time.time_since_epoch()));
}

duration_c from_chrono(const std::chrono::nanoseconds duration) noexcept
{
  return duration_c::from_nanoseconds(duration.count());
}

duration_c from_chrono(const std::chrono::microseconds duration) noexcept
{
  return duration_c::from_microseconds(duration.count());
}

duration_c from_chrono(const std::chrono::milliseconds duration) noexcept
{
  return duration_c::from_milliseconds(duration.count());
}

duration_c from_chrono(const std::chrono::seconds duration) noexcept
{
  return duration_c::from_seconds(duration.count());
}

std::string_view clock_name(const clock_e clock) noexcept
{
  switch (clock) {
    case clock_e::system:
      return "system";
    case clock_e::steady:
      return "steady";
    case clock_e::ros:
      return "ros";
  }
  return "unknown";
}

string64_t serialize_time(const system_time_c time) noexcept
{
  return serialize_ticks(clock_e::system, time.nanoseconds());
}

string64_t serialize_time(const steady_time_c time) noexcept
{
  return serialize_ticks(clock_e::steady, time.nanoseconds());
}

string64_t serialize_time(const ros_time_c time) noexcept
{
  return serialize_ticks(clock_e::ros, time.nanoseconds());
}

std::optional<system_time_c> deserialize_system_time(
  const std::string_view serialized) noexcept
{
  return deserialize_time<clock_e::system>(serialized);
}

std::optional<steady_time_c> deserialize_steady_time(
  const std::string_view serialized) noexcept
{
  return deserialize_time<clock_e::steady>(serialized);
}

std::optional<ros_time_c> deserialize_ros_time(
  const std::string_view serialized) noexcept
{
  return deserialize_time<clock_e::ros>(serialized);
}

string64_t format_utc(const system_time_c time) noexcept
{
  string64_t formatted;
  const std::optional<struct timespec> posix_time = to_timespec(time);
  if (!posix_time.has_value()) {
    return formatted;
  }

  const std::time_t seconds = posix_time->tv_sec;
  struct tm utc_time {};
  if (::gmtime_r(&seconds, &utc_time) == nullptr) {
    return formatted;
  }

  char buffer[string64_t::capacity() + 1U]{};
  const int32_t written = static_cast<int32_t>(std::snprintf(
    buffer,
    sizeof(buffer),
    "%04d-%02d-%02dT%02d:%02d:%02d.%09dZ",
    utc_time.tm_year + 1900,
    utc_time.tm_mon + 1,
    utc_time.tm_mday,
    utc_time.tm_hour,
    utc_time.tm_min,
    utc_time.tm_sec,
    static_cast<int32_t>(posix_time->tv_nsec)));
  if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(buffer)) {
    return formatted;
  }
  formatted = std::string_view(buffer, static_cast<std::size_t>(written));
  return formatted;
}

}  // namespace time
}  // namespace common
