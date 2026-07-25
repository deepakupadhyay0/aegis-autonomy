#pragma once

#include <pthread.h>
#include <cstdint>
#include <stdexcept>
#include <cerrno>
#include <ctime>

#include "base_node/core_defs.hpp"
#include "base_node/visibility_control.hpp"

namespace base_node
{
namespace sync
{

/// @brief A high-performance recursive mutex natively wrapping POSIX pthread_mutex_t.
/// This guarantees real-time priority inheritance and O(1) locking speed natively in C++,
/// completely severing the dependency on C-structs.
class BASE_NODE_PUBLIC mutex_c
{
public:
  mutex_c();
  ~mutex_c() noexcept;

  mutex_c(const mutex_c&) = delete;
  mutex_c& operator=(const mutex_c&) = delete;
  mutex_c(mutex_c&&) = delete;
  mutex_c& operator=(mutex_c&&) = delete;

  bool is_locked_by_other() const;
  bool is_locked_by_me() const;
  bool is_locked() const;

  core_ret_e timedlock_ms(int64_t const timeout_ms);

  void lock();
  void unlock();

private:
  uint64_t m_lock_counter;
  pthread_t m_owner_tid;
  pthread_mutex_t m_mutex;
  pthread_mutexattr_t m_attr;
};

}  // namespace sync
}  // namespace base_node
