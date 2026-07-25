#include "base_node/mutex.hpp"

namespace base_node
{
namespace sync
{

mutex_c::mutex_c()
: m_lock_counter(0),
  m_owner_tid(0)
{
  int32_t posix_error = pthread_mutexattr_init(&m_attr);
  if (posix_error != 0) {
    throw std::runtime_error("pthread_mutexattr_init failed");
  }

#ifdef _POSIX_THREAD_PRIO_INHERIT
  // Attempt priority inheritance if the RTOS/kernel supports it
  pthread_mutexattr_setprotocol(&m_attr, PTHREAD_PRIO_INHERIT);
#endif

  posix_error = pthread_mutex_init(&m_mutex, &m_attr);
  if (posix_error != 0) {
    pthread_mutexattr_destroy(&m_attr);
    throw std::runtime_error("pthread_mutex_init failed");
  }
}

mutex_c::~mutex_c() noexcept
{
  if (is_locked_by_me()) {
    unlock();
  }
  pthread_mutex_destroy(&m_mutex);
  pthread_mutexattr_destroy(&m_attr);
}

bool mutex_c::is_locked_by_other() const
{
  if (m_lock_counter != 0) {
    return pthread_equal(m_owner_tid, pthread_self()) == 0;
  }
  return false;
}

bool mutex_c::is_locked_by_me() const
{
  if (m_lock_counter != 0) {
    return pthread_equal(m_owner_tid, pthread_self()) != 0;
  }
  return false;
}

bool mutex_c::is_locked() const
{
  return m_lock_counter != 0;
}

core_ret_e mutex_c::timedlock_ms(int64_t const timeout_ms)
{
  if (timeout_ms < 0) {
    // Indefinite block
    if (is_locked_by_me()) {
      m_lock_counter++;
      return core_ret_e::ok;
    }
    
    int32_t posix_error = pthread_mutex_lock(&m_mutex);
    if (posix_error == 0) {
      m_owner_tid = pthread_self();
      m_lock_counter = 1;
      return core_ret_e::ok;
    }
    return core_ret_e::error;
  }

  if (is_locked_by_me()) {
    m_lock_counter++;
    return core_ret_e::ok;
  }

  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);

  struct timespec end_time;
  end_time.tv_sec = now.tv_sec + (timeout_ms / 1000);
  end_time.tv_nsec = now.tv_nsec + ((timeout_ms % 1000) * 1000000);
  
  if (end_time.tv_nsec >= 1000000000) {
    end_time.tv_sec += 1;
    end_time.tv_nsec -= 1000000000;
  }

  int32_t posix_error = pthread_mutex_timedlock(&m_mutex, &end_time);

  if (posix_error == 0) {
    m_owner_tid = pthread_self();
    m_lock_counter = 1;
    return core_ret_e::ok;
  } else if (posix_error == ETIMEDOUT) {
    return core_ret_e::timeout;
  }

  return core_ret_e::error;
}

void mutex_c::lock()
{
  timedlock_ms(-1);
}

void mutex_c::unlock()
{
  if (is_locked_by_me()) {
    m_lock_counter--;
    if (m_lock_counter == 0) {
      m_owner_tid = 0;
      pthread_mutex_unlock(&m_mutex);
    }
  }
}

}  // namespace sync
}  // namespace base_node
