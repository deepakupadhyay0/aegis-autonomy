#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace logging
{

class log_throttle_c final
{
public:
  log_throttle_c() noexcept = default;
  ~log_throttle_c() noexcept = default;

  log_throttle_c(const log_throttle_c &) = delete;
  log_throttle_c & operator=(const log_throttle_c &) = delete;
  log_throttle_c(log_throttle_c &&) = delete;
  log_throttle_c & operator=(log_throttle_c &&) = delete;

  template<class rep_t, class period_t>
  bool ready(
    const std::chrono::duration<rep_t, period_t> interval) noexcept
  {
    const int64_t signed_interval_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(interval).count();
    if (signed_interval_ns <= 0) {
      return true;
    }

    const uint64_t current_time_ns = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    const uint64_t interval_ns = static_cast<uint64_t>(signed_interval_ns);
    uint64_t next_time_ns = m_next_time_ns.load(std::memory_order_relaxed);
    if (current_time_ns < next_time_ns) {
      return false;
    }

    // The atomic only elects one caller per interval; it publishes no data.
    return m_next_time_ns.compare_exchange_strong(
      next_time_ns,
      current_time_ns + interval_ns,
      std::memory_order_relaxed,
      std::memory_order_relaxed);
  }

private:
  std::atomic<uint64_t> m_next_time_ns{0U};
};

}  // namespace logging
