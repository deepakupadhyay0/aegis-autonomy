#include "base_core/execution/thread_scheduling.hpp"

#include <cerrno>
#include <pthread.h>

namespace base_core
{
namespace execution
{
namespace
{

int32_t get_native_policy(const scheduler_policy_e policy) noexcept
{
  switch (policy) {
    case scheduler_policy_e::other:
      return SCHED_OTHER;
    case scheduler_policy_e::fifo:
      return SCHED_FIFO;
    case scheduler_policy_e::round_robin:
      return SCHED_RR;
    case scheduler_policy_e::inherited:
    default:
      return -1;
  }
}

}  // namespace

thread_scheduling_guard_c::thread_scheduling_guard_c(
  const thread_scheduling_options_s & options) noexcept
: m_original_policy(SCHED_OTHER),
  m_original_parameters{},
  m_posix_error(0),
  m_restore_required(false)
{
  if (options.policy == scheduler_policy_e::inherited) {
    return;
  }

  m_posix_error = pthread_getschedparam(
    pthread_self(),
    &m_original_policy,
    &m_original_parameters);
  if (m_posix_error != 0) {
    return;
  }

  const int32_t native_policy = get_native_policy(options.policy);
  if (native_policy < 0) {
    m_posix_error = EINVAL;
    return;
  }

  const int32_t minimum_priority = sched_get_priority_min(native_policy);
  const int32_t maximum_priority = sched_get_priority_max(native_policy);
  if (minimum_priority < 0 ||
    maximum_priority < 0 ||
    options.priority < minimum_priority ||
    options.priority > maximum_priority)
  {
    m_posix_error = EINVAL;
    return;
  }

  sched_param scheduling_parameters{};
  scheduling_parameters.sched_priority = options.priority;
  m_posix_error = pthread_setschedparam(
    pthread_self(),
    native_policy,
    &scheduling_parameters);
  m_restore_required = m_posix_error == 0;
}

thread_scheduling_guard_c::~thread_scheduling_guard_c() noexcept
{
  if (m_restore_required) {
    static_cast<void>(
      pthread_setschedparam(
        pthread_self(),
        m_original_policy,
        &m_original_parameters));
  }
}

int32_t thread_scheduling_guard_c::get_posix_error() const noexcept
{
  return m_posix_error;
}

}  // namespace execution
}  // namespace base_core
