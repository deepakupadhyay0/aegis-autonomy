#include "base_core/mutex.hpp"

#include <cerrno>
#include <ctime>
#include <stdexcept>
#include <unistd.h>

namespace base_core
{
namespace sync
{

mutex_c::mutex_c(const priority_inheritance_e priority_inheritance)
: m_attributes(),
  m_mutex(),
  m_lock_depth(0U),
  m_attributes_initialized(false),
  m_mutex_initialized(false)
{
  int32_t result = ::pthread_mutexattr_init(&m_attributes);
  if (result != 0) {
    throw std::runtime_error("pthread_mutexattr_init failed");
  }
  m_attributes_initialized = true;

  if (priority_inheritance == priority_inheritance_e::enabled) {
#ifdef _POSIX_THREAD_PRIO_INHERIT
    result = ::pthread_mutexattr_setprotocol(&m_attributes, PTHREAD_PRIO_INHERIT);
    if (result != 0) {
      this->cleanup();
      throw std::runtime_error("pthread_mutexattr_setprotocol failed");
    }
#else
    this->cleanup();
    throw std::runtime_error("Priority inheritance is unavailable");
#endif
  }

  result = ::pthread_mutex_init(&m_mutex, &m_attributes);
  if (result != 0) {
    this->cleanup();
    throw std::runtime_error("pthread_mutex_init failed");
  }
  m_mutex_initialized = true;
}

mutex_c::~mutex_c() noexcept
{
  this->cleanup();
}

void mutex_c::cleanup() noexcept
{
  if (m_mutex_initialized) {
    ::pthread_mutex_destroy(&m_mutex);
    m_mutex_initialized = false;
  }
  if (m_attributes_initialized) {
    ::pthread_mutexattr_destroy(&m_attributes);
    m_attributes_initialized = false;
  }
}

bool8_t mutex_c::is_locked() const noexcept
{
  return m_lock_depth.load(std::memory_order_relaxed) != 0U;
}

core_ret_e mutex_c::timedlock_ms(const int64_t timeout_ms)
{
  int32_t result = 0;
  if (timeout_ms < 0) {
    result = ::pthread_mutex_lock(&m_mutex);
  } else {
    struct timespec end_time = {};
    if (::clock_gettime(CLOCK_REALTIME, &end_time) != 0) {
      return core_ret_e::error;
    }

    end_time.tv_sec += static_cast<time_t>(timeout_ms / 1000);
    end_time.tv_nsec += static_cast<long>((timeout_ms % 1000) * 1000000);
    if (end_time.tv_nsec >= 1000000000L) {
      end_time.tv_sec += 1;
      end_time.tv_nsec -= 1000000000L;
    }
    result = ::pthread_mutex_timedlock(&m_mutex, &end_time);
  }

  if (result == 0) {
    m_lock_depth.fetch_add(1U, std::memory_order_relaxed);
    return core_ret_e::ok;
  }
  if (result == ETIMEDOUT) {
    return core_ret_e::timeout;
  }
  return core_ret_e::error;
}

void mutex_c::lock()
{
  if (this->timedlock_ms(-1) != core_ret_e::ok) {
    throw std::runtime_error("pthread_mutex_lock failed");
  }
}

void mutex_c::unlock() noexcept
{
  const int32_t result = ::pthread_mutex_unlock(&m_mutex);
  if (result == 0) {
    m_lock_depth.fetch_sub(1U, std::memory_order_relaxed);
  }
}

}  // namespace sync
}  // namespace base_core
