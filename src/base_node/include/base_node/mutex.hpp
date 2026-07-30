#pragma once

#include <pthread.h>

#include <atomic>
#include <cstdint>

#include "base_node/core_defs.hpp"
#include "base_node/visibility_control.hpp"

namespace base_node
{
namespace sync
{

enum class priority_inheritance_e : uint8_t
{
  disabled = 0U,
  enabled = 1U
};

/// @brief C++ RAII facade for a POSIX mutex.
class BASE_NODE_PUBLIC mutex_c
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

}  // namespace sync
}  // namespace base_node
