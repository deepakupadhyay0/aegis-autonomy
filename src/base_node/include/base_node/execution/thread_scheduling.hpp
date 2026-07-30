#pragma once

#include "base_node/core_defs.hpp"
#include "base_node/visibility_control.hpp"

#include <cstdint>
#include <sched.h>

namespace base_node
{
namespace execution
{

enum class scheduler_policy_e : uint8_t
{
  inherited = 0U,
  other,
  fifo,
  round_robin
};

struct thread_scheduling_options_s
{
  scheduler_policy_e policy{scheduler_policy_e::inherited};
  int32_t priority{0};
  bool8_t required{false};
};

class BASE_NODE_PUBLIC thread_scheduling_guard_c final
{
public:
  explicit thread_scheduling_guard_c(
    const thread_scheduling_options_s & options) noexcept;
  ~thread_scheduling_guard_c() noexcept;

  thread_scheduling_guard_c(const thread_scheduling_guard_c &) = delete;
  thread_scheduling_guard_c & operator=(
    const thread_scheduling_guard_c &) = delete;
  thread_scheduling_guard_c(thread_scheduling_guard_c &&) = delete;
  thread_scheduling_guard_c & operator=(
    thread_scheduling_guard_c &&) = delete;

  int32_t get_posix_error() const noexcept;

private:
  int32_t m_original_policy;
  sched_param m_original_parameters;
  int32_t m_posix_error;
  bool8_t m_restore_required;
};

}  // namespace execution
}  // namespace base_node
