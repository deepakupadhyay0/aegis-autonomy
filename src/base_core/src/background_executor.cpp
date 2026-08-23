#include "base_core/execution/background_executor.hpp"
#include "base_core/mutex.hpp"

#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

#if defined(__linux__)
#include <sys/resource.h>
#endif

namespace base_core::execution
{
namespace
{

struct creator_priority_s
{
  common::int32_t nice_value{0};
  bool valid{false};
};

creator_priority_s read_creator_priority() noexcept
{
#if defined(__linux__)
  errno = 0;
  const common::int32_t value = getpriority(PRIO_PROCESS, 0U);
  if (value == -1 && errno != 0) {
    return {};
  }
  return creator_priority_s{value, true};
#else
  return {};
#endif
}

void apply_worker_priority(const creator_priority_s & priority) noexcept
{
#if defined(__linux__)
  if (!priority.valid) {
    return;
  }
  const common::int32_t worker_nice =
    detail::calculate_background_nice(priority.nice_value);
  static_cast<void>(setpriority(PRIO_PROCESS, 0U, worker_nice));
#else
  static_cast<void>(priority);
#endif
}

}  // namespace

class background_executor_c::implementation_c final
{
public:
  explicit implementation_c(const std::size_t queue_capacity)
  : m_capacity(validate_capacity(queue_capacity)),
    m_priority(read_creator_priority()),
    m_mutex(sync::priority_inheritance_e::disabled),
    m_shutdown_mutex(sync::priority_inheritance_e::disabled),
    m_worker([this](const std::stop_token &) {this->run();})
  {
  }

  ~implementation_c() noexcept = default;

  bool try_post(task_t task)
  {
    if (!task) {
      return false;
    }
    {
      const std::lock_guard<sync::mutex_c> lock(m_mutex);
      if (m_shutdown_started || m_tasks.size() == m_capacity) {
        return false;
      }
      m_tasks.push_back(std::move(task));
    }
    m_condition.notify_one();
    return true;
  }

  void shutdown(const shutdown_mode_e mode) noexcept
  {
    if (std::this_thread::get_id() == m_worker.get_id()) {
      return;
    }
    const std::lock_guard<sync::mutex_c> shutdown_lock(m_shutdown_mutex);
    {
      const std::lock_guard<sync::mutex_c> lock(m_mutex);
      if (!m_shutdown_started) {
        m_shutdown_started = true;
        m_shutdown_mode = mode;
        if (mode == shutdown_mode_e::cancel_pending) {
          m_tasks.clear();
        }
      } else if (mode == shutdown_mode_e::cancel_pending) {
        m_shutdown_mode = mode;
        m_tasks.clear();
      }
    }
    m_condition.notify_all();
    if (m_worker.joinable()) {
      m_worker.join();
    }
  }

private:
  static std::size_t validate_capacity(const std::size_t capacity)
  {
    if (capacity == 0U) {
      throw std::invalid_argument("Background executor capacity must be positive");
    }
    return capacity;
  }

  void run() noexcept
  {
    apply_worker_priority(m_priority);
    while (true) {
      task_t task;
      {
        std::unique_lock<sync::mutex_c> lock(m_mutex);
        m_condition.wait(lock, [this]() {
            return m_shutdown_started || !m_tasks.empty();
        });
        if (m_shutdown_started &&
          (m_shutdown_mode == shutdown_mode_e::cancel_pending || m_tasks.empty()))
        {
          return;
        }
        task = std::move(m_tasks.front());
        m_tasks.pop_front();
      }
      try {
        task();
      } catch (...) {  // NOLINT(bugprone-empty-catch)
        // Tasks own their error reporting; prevent escaped exceptions from
        // terminating the worker.
      }
    }
  }

  const std::size_t m_capacity;
  const creator_priority_s m_priority;
  sync::mutex_c m_mutex;
  sync::mutex_c m_shutdown_mutex;
  std::condition_variable_any m_condition;
  std::deque<task_t> m_tasks;
  bool m_shutdown_started{false};
  shutdown_mode_e m_shutdown_mode{shutdown_mode_e::cancel_pending};
  std::jthread m_worker;
};

class task_scope_c::state_c final
{
public:
  state_c()
  : m_mutex(sync::priority_inheritance_e::disabled)
  {
  }

  bool try_post(
    background_executor_c & executor,
    const std::shared_ptr<state_c> & state,
    background_executor_c::task_t task)
  {
    const std::lock_guard<sync::mutex_c> lock(m_mutex);
    if (m_cancelled) {
      return false;
    }
    return executor.try_post(
      [state, task = std::move(task)]() mutable {
        if (!state->try_start()) {
          return;
        }
        try {
          task();
        } catch (...) {
          state->finish();
          throw;
        }
        state->finish();
      });
  }

  bool try_start() noexcept
  {
    const std::lock_guard<sync::mutex_c> lock(m_mutex);
    if (m_cancelled) {
      return false;
    }
    ++m_active_count;
    return true;
  }

  void finish() noexcept
  {
    {
      const std::lock_guard<sync::mutex_c> lock(m_mutex);
      --m_active_count;
    }
    m_condition.notify_all();
  }

  void cancel_and_wait() noexcept
  {
    std::unique_lock<sync::mutex_c> lock(m_mutex);
    m_cancelled = true;
    m_condition.wait(lock, [this]() {return m_active_count == 0U;});
  }

  sync::mutex_c m_mutex;
  std::condition_variable_any m_condition;
  std::size_t m_active_count{0U};
  bool m_cancelled{false};
};

background_executor_c::background_executor_c(const std::size_t queue_capacity)
: m_implementation(std::make_unique<implementation_c>(queue_capacity))
{
}

background_executor_c::~background_executor_c() noexcept
{
  m_implementation->shutdown(shutdown_mode_e::cancel_pending);
}

bool background_executor_c::try_post(task_t task)
{
  return m_implementation->try_post(std::move(task));
}

void background_executor_c::shutdown(const shutdown_mode_e mode) noexcept
{
  m_implementation->shutdown(mode);
}

task_scope_c::task_scope_c(background_executor_c & executor)
: m_executor(executor), m_state(std::make_shared<state_c>())
{
}

task_scope_c::~task_scope_c() noexcept
{
  this->cancel_and_wait();
}

bool task_scope_c::try_post(background_executor_c::task_t task)
{
  if (!task) {
    return false;
  }
  const std::shared_ptr<state_c> state = m_state;
  return state->try_post(m_executor, state, std::move(task));
}

void task_scope_c::cancel_and_wait() noexcept
{
  m_state->cancel_and_wait();
}

}  // namespace base_core::execution
