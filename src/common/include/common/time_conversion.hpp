#pragma once

#include "common/fixed_string.hpp"
#include "common/time.hpp"

#include <chrono>
#include <ctime>
#include <optional>
#include <string_view>

namespace common
{
namespace time
{

struct seconds_nanoseconds_s
{
  int64_t seconds{0LL};
  int32_t nanoseconds{0};
};

/// @brief Splits ticks using floor normalization.
/// @return Empty for invalid ticks; otherwise nanoseconds is in [0, 1e9).
std::optional<seconds_nanoseconds_s> split_time(const int64_t ticks) noexcept;

std::optional<struct timespec> to_timespec(const duration_c duration) noexcept;
std::optional<struct timespec> to_timespec(const system_time_c time) noexcept;
std::optional<struct timespec> to_timespec(const steady_time_c time) noexcept;

duration_c duration_from_timespec(const struct timespec & time) noexcept;
system_time_c system_time_from_timespec(const struct timespec & time) noexcept;
steady_time_c steady_time_from_timespec(const struct timespec & time) noexcept;

std::optional<std::chrono::nanoseconds> to_chrono(
  const duration_c duration) noexcept;
std::optional<std::chrono::system_clock::time_point> to_chrono(
  const system_time_c time) noexcept;
std::optional<std::chrono::steady_clock::time_point> to_chrono(
  const steady_time_c time) noexcept;

system_time_c from_chrono(
  const std::chrono::system_clock::time_point time) noexcept;
steady_time_c from_chrono(
  const std::chrono::steady_clock::time_point time) noexcept;

duration_c from_chrono(const std::chrono::nanoseconds duration) noexcept;
duration_c from_chrono(const std::chrono::microseconds duration) noexcept;
duration_c from_chrono(const std::chrono::milliseconds duration) noexcept;
duration_c from_chrono(const std::chrono::seconds duration) noexcept;

std::string_view clock_name(const clock_e clock) noexcept;

string64_t serialize_time(const system_time_c time) noexcept;
string64_t serialize_time(const steady_time_c time) noexcept;
string64_t serialize_time(const ros_time_c time) noexcept;

std::optional<system_time_c> deserialize_system_time(
  const std::string_view serialized) noexcept;
std::optional<steady_time_c> deserialize_steady_time(
  const std::string_view serialized) noexcept;
std::optional<ros_time_c> deserialize_ros_time(
  const std::string_view serialized) noexcept;

/// @brief Formats system time as UTC with nanosecond precision.
/// @details This calendar conversion is not intended for real-time paths.
string64_t format_utc(const system_time_c time) noexcept;

}  // namespace time
}  // namespace common
