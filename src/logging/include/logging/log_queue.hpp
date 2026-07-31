#pragma once

#include "common/logging/log_protocol.hpp"
#include "logging/visibility_control.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace logging
{

/// Fixed-capacity MPSC queue. Producers never wait: contention drops the new
/// record, while capacity overflow overwrites the oldest record.
class LOGGING_PUBLIC log_queue_c final
{
public:
  explicit log_queue_c(std::size_t capacity);
  ~log_queue_c() noexcept;

  log_queue_c(const log_queue_c &) = delete;
  log_queue_c & operator=(const log_queue_c &) = delete;
  log_queue_c(log_queue_c &&) = delete;
  log_queue_c & operator=(log_queue_c &&) = delete;

  bool try_push(common::logging::log_record_s && record) noexcept;
  bool wait_and_pop(common::logging::log_record_s & record) noexcept;
  uint64_t get_dropped_record_count() const noexcept;
  void shutdown() noexcept;

private:
  bool try_pop(common::logging::log_record_s & record) noexcept;
  void unlock_queue() noexcept;

  std::vector<common::logging::log_record_s> m_storage;
  std::size_t m_head;
  std::size_t m_tail;
  std::size_t m_size;
  std::atomic_flag m_queue_lock = ATOMIC_FLAG_INIT;
  std::atomic<uint64_t> m_generation;
  std::atomic<uint64_t> m_dropped_record_count;
  std::atomic<bool> m_running;
};

}  // namespace logging
