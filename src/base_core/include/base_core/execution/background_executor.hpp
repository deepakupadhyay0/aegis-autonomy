#pragma once

#include "base_core/visibility_control.hpp"
#include "common/numeric_types.hpp"

#include <cstddef>
#include <functional>
#include <memory>

namespace base_core::execution
{

namespace detail
{

constexpr common::int32_t calculate_background_nice(
  const common::int32_t creator_nice) noexcept
{
  return creator_nice >= 18 ? 19 : creator_nice + 1;
}

}  // namespace detail

enum class shutdown_mode_e : common::uint8_t
{
  drain = 0U,
  cancel_pending
};

/// Bounded, single-worker executor for non-real-time background work.
///
/// shutdown() must not be called from a task running on this executor. The
/// destructor performs cancel_pending shutdown and joins the worker.
class BASE_CORE_PUBLIC background_executor_c final
{
public:
  using task_t = std::function<void()>;

  explicit background_executor_c(std::size_t queue_capacity);
  ~background_executor_c() noexcept;

  background_executor_c(const background_executor_c &) = delete;
  background_executor_c & operator=(const background_executor_c &) = delete;
  background_executor_c(background_executor_c &&) = delete;
  background_executor_c & operator=(background_executor_c &&) = delete;

  bool try_post(task_t task);
  void shutdown(shutdown_mode_e mode) noexcept;

private:
  class implementation_c;
  std::unique_ptr<implementation_c> m_implementation;
};

/// Lifecycle guard for tasks posted to a longer-lived executor.
///
/// The referenced executor must outlive this scope. Cancelling one scope does
/// not affect unscoped tasks or tasks belonging to another scope.
class BASE_CORE_PUBLIC task_scope_c final
{
public:
  explicit task_scope_c(background_executor_c & executor);
  ~task_scope_c() noexcept;

  task_scope_c(const task_scope_c &) = delete;
  task_scope_c & operator=(const task_scope_c &) = delete;
  task_scope_c(task_scope_c &&) = delete;
  task_scope_c & operator=(task_scope_c &&) = delete;

  bool try_post(background_executor_c::task_t task);
  void cancel_and_wait() noexcept;

private:
  class state_c;
  background_executor_c & m_executor;
  std::shared_ptr<state_c> m_state;
};

}  // namespace base_core::execution
