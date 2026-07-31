#pragma once

#include "logging/log_types.hpp"
#include "logging/visibility_control.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace logging
{

struct log_queue_statistics_s
{
  uint64_t capacity{0U};
  uint64_t occupancy{0U};
  uint64_t peak_occupancy{0U};
  uint64_t accepted_record_count{0U};
  uint64_t dropped_capacity_count{0U};
  uint64_t dropped_contention_count{0U};
  uint64_t dropped_stopped_count{0U};
  uint64_t pressure_event_count{0U};
};

/// Fixed-capacity MPSC queue. Producers never wait and existing records are
/// never overwritten. Contention or full capacity rejects the incoming record.
class LOGGING_PUBLIC log_queue_c final
{
public:
  explicit log_queue_c(std::size_t capacity);
  ~log_queue_c() noexcept;

  log_queue_c(const log_queue_c &) = delete;
  log_queue_c & operator=(const log_queue_c &) = delete;
  log_queue_c(log_queue_c &&) = delete;
  log_queue_c & operator=(log_queue_c &&) = delete;

  bool try_push(log_record_s && record) noexcept;
  bool wait_and_pop(log_record_s & record) noexcept;
  uint64_t get_dropped_record_count() const noexcept;
  log_queue_statistics_s get_statistics() const noexcept;
  bool consume_pressure_request() noexcept;
  void shutdown() noexcept;

private:
  bool try_pop(log_record_s & record) noexcept;
  void unlock_queue() noexcept;

  std::vector<log_record_s> m_storage;
  std::size_t m_head;
  std::size_t m_tail;
  std::size_t m_size;
  const std::size_t m_high_watermark;
  const std::size_t m_low_watermark;
  bool m_pressure_active;
  std::atomic_flag m_queue_lock = ATOMIC_FLAG_INIT;
  /// Release/acquire generation synchronizes record publication and wake-up.
  /// Statistics use relaxed ordering unless they represent live occupancy.
  std::atomic<uint64_t> m_generation;
  std::atomic<uint64_t> m_occupancy;
  std::atomic<uint64_t> m_peak_occupancy;
  std::atomic<uint64_t> m_accepted_record_count;
  std::atomic<uint64_t> m_dropped_capacity_count;
  std::atomic<uint64_t> m_dropped_contention_count;
  std::atomic<uint64_t> m_dropped_stopped_count;
  std::atomic<uint64_t> m_pressure_event_count;
  std::atomic<bool> m_pressure_requested;
  std::atomic<bool> m_running;
};

}  // namespace logging
