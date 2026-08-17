#include "common/time.hpp"

#include <ctime>

namespace common
{
namespace time
{
namespace
{

template<clock_e clock_v>
basic_time_c<clock_v> clock_now(const int32_t clock_id) noexcept
{
  struct timespec current_time {};
  if (::clock_gettime(static_cast<clockid_t>(clock_id), &current_time) != 0) {
    return basic_time_c<clock_v>::invalid();
  }
  return basic_time_c<clock_v>::from_seconds_nanoseconds(
    static_cast<int64_t>(current_time.tv_sec),
    static_cast<int64_t>(current_time.tv_nsec));
}

}  // namespace

system_time_c system_now() noexcept
{
  return clock_now<clock_e::system>(CLOCK_REALTIME);
}

steady_time_c steady_now() noexcept
{
  return clock_now<clock_e::steady>(CLOCK_MONOTONIC);
}

}  // namespace time
}  // namespace common
