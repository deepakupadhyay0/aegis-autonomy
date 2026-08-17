#pragma once

#include <pthread.h>

#include <atomic>
#include <chrono>
#include <cstdint>

#include "base_core/core_defs.hpp"
#include "base_core/visibility_control.hpp"

namespace base_core
{
namespace sync
{

enum class priority_inheritance_e : uint8_t
{
  disabled = 0U,
  enabled = 1U
};

/// @brief C++ RAII facade for a POSIX mutex.
class BASE_CORE_PUBLIC mutex_c
{
public:
  explicit mutex_c(priority_inheritance_e priority_inheritance);
  ~mutex_c() noexcept;

  mutex_c(const mutex_c&) = delete;
  mutex_c& operator=(const mutex_c&) = delete;
  mutex_c(mutex_c&&) = delete;
  mutex_c& operator=(mutex_c&&) = delete;

  bool8_t is_locked() const noexcept;

  core_ret_e timedlock_ms(int64_t const timeout_ms);

  void lock();
  void unlock() noexcept;

private:
  void cleanup() noexcept;

  pthread_mutexattr_t m_attributes;
  pthread_mutex_t m_mutex;
  std::atomic<uint32_t> m_lock_depth;
  bool8_t m_attributes_initialized;
  bool8_t m_mutex_initialized;
};

/// @brief Mutex extension that bounds each lock acquisition request.
class BASE_CORE_PUBLIC timed_mutex_c final : public mutex_c
{
public:
  timed_mutex_c(
    priority_inheritance_e priority_inheritance,
    std::chrono::milliseconds acquisition_timeout);

  timed_mutex_c(const timed_mutex_c &) = delete;
  timed_mutex_c & operator=(const timed_mutex_c &) = delete;
  timed_mutex_c(timed_mutex_c &&) = delete;
  timed_mutex_c & operator=(timed_mutex_c &&) = delete;

  /// @throws std::system_error when the acquisition request times out.
  /// @throws std::runtime_error when the native lock operation fails.
  void lock();

private:
  const std::chrono::milliseconds m_acquisition_timeout;
};

}  // namespace sync
}  // namespace base_core
