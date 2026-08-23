#include "base_core/execution/background_executor.hpp"
#include "base_core/mutex.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <algorithm>
#include <cstddef>
#include <latch>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{

using base_core::execution::background_executor_c;
using base_core::execution::shutdown_mode_e;
using base_core::execution::task_scope_c;

TEST(BackgroundExecutorTest, ExecutesTasksInFifoOrder)
{
  background_executor_c executor(4U);
  base_core::sync::mutex_c mutex(
    base_core::sync::priority_inheritance_e::disabled);
  std::vector<std::size_t> order;
  std::latch completed(3);

  for (std::size_t index = 0U; index < 3U; ++index) {
    ASSERT_TRUE(executor.try_post([&mutex, &order, &completed, index]() {
        const std::lock_guard<base_core::sync::mutex_c> lock(mutex);
        order.push_back(index);
        completed.count_down();
    }));
  }
  completed.wait();
  executor.shutdown(shutdown_mode_e::drain);
  EXPECT_EQ(order, (std::vector<std::size_t>{0U, 1U, 2U}));
}

TEST(BackgroundExecutorTest, ExecutesOnlyOneTaskAtATime)
{
  background_executor_c executor(4U);
  std::atomic<std::size_t> active{0U};
  std::atomic<std::size_t> maximum_active{0U};
  std::latch completed(3);
  for (std::size_t index = 0U; index < 3U; ++index) {
    static_cast<void>(index);
    ASSERT_TRUE(executor.try_post([&active, &maximum_active, &completed]() {
        const std::size_t current = active.fetch_add(1U) + 1U;
        maximum_active.store(std::max(maximum_active.load(), current));
        active.fetch_sub(1U);
        completed.count_down();
    }));
  }
  completed.wait();
  executor.shutdown(shutdown_mode_e::drain);
  EXPECT_EQ(maximum_active.load(), 1U);
}

TEST(BackgroundExecutorTest, RejectsWhenQueueIsFull)
{
  background_executor_c executor(1U);
  std::latch active(1);
  std::latch release(1);
  ASSERT_TRUE(executor.try_post([&active, &release]() {
      active.count_down();
      release.wait();
  }));
  active.wait();
  ASSERT_TRUE(executor.try_post([]() {}));
  EXPECT_FALSE(executor.try_post([]() {}));
  release.count_down();
  executor.shutdown(shutdown_mode_e::cancel_pending);
}

TEST(BackgroundExecutorTest, RejectsPostingAfterShutdown)
{
  background_executor_c executor(1U);
  executor.shutdown(shutdown_mode_e::drain);
  EXPECT_FALSE(executor.try_post([]() {}));
  executor.shutdown(shutdown_mode_e::cancel_pending);
}

TEST(BackgroundExecutorTest, TaskCanPostAnotherTask)
{
  background_executor_c executor(2U);
  std::latch completed(1);
  ASSERT_TRUE(executor.try_post([&executor, &completed]() {
      EXPECT_TRUE(executor.try_post([&completed]() {completed.count_down();}));
  }));
  completed.wait();
  executor.shutdown(shutdown_mode_e::drain);
}

TEST(BackgroundExecutorTest, ContinuesAfterTaskException)
{
  background_executor_c executor(2U);
  std::latch completed(1);
  ASSERT_TRUE(executor.try_post([]() {throw std::runtime_error("expected");}));
  ASSERT_TRUE(executor.try_post([&completed]() {completed.count_down();}));
  completed.wait();
  executor.shutdown(shutdown_mode_e::drain);
}

TEST(BackgroundExecutorTest, DrainCompletesAcceptedTasks)
{
  background_executor_c executor(3U);
  std::atomic<std::size_t> executed{0U};
  for (std::size_t index = 0U; index < 3U; ++index) {
    static_cast<void>(index);
    ASSERT_TRUE(executor.try_post([&executed]() {executed.fetch_add(1U);}));
  }
  executor.shutdown(shutdown_mode_e::drain);
  EXPECT_EQ(executed.load(), 3U);
}

TEST(BackgroundExecutorTest, CancelPendingWaitsForActiveAndDiscardsQueued)
{
  background_executor_c executor(2U);
  std::latch active(1);
  std::latch release(1);
  std::atomic<bool> pending_executed{false};
  ASSERT_TRUE(executor.try_post([&active, &release]() {
      active.count_down();
      release.wait();
  }));
  active.wait();
  ASSERT_TRUE(executor.try_post([&pending_executed]() {pending_executed = true;}));
  std::latch shutdown_started(1);
  std::latch shutdown_finished(1);
  std::jthread shutdown_thread([&]() {
      shutdown_started.count_down();
      executor.shutdown(shutdown_mode_e::cancel_pending);
      shutdown_finished.count_down();
    });
  shutdown_started.wait();
  EXPECT_FALSE(shutdown_finished.try_wait());
  release.count_down();
  shutdown_finished.wait();
  EXPECT_FALSE(pending_executed.load());
}

TEST(TaskScopeTest, DestructionPreventsPendingTaskFromStarting)
{
  background_executor_c executor(2U);
  std::latch blocker_active(1);
  std::latch release_blocker(1);
  std::atomic<bool> scoped_executed{false};
  ASSERT_TRUE(executor.try_post([&]() {
      blocker_active.count_down();
      release_blocker.wait();
  }));
  blocker_active.wait();
  {
    task_scope_c scope(executor);
    ASSERT_TRUE(scope.try_post([&scoped_executed]() {scoped_executed = true;}));
  }
  release_blocker.count_down();
  executor.shutdown(shutdown_mode_e::drain);
  EXPECT_FALSE(scoped_executed.load());
}

TEST(TaskScopeTest, CancelAndWaitWaitsForActiveTask)
{
  background_executor_c executor(1U);
  task_scope_c scope(executor);
  std::latch active(1);
  std::latch release(1);
  ASSERT_TRUE(scope.try_post([&]() {
      active.count_down();
      release.wait();
  }));
  active.wait();
  std::latch cancelled(1);
  std::jthread cancel_thread([&]() {
      scope.cancel_and_wait();
      cancelled.count_down();
    });
  EXPECT_FALSE(cancelled.try_wait());
  release.count_down();
  cancelled.wait();
  executor.shutdown(shutdown_mode_e::drain);
}

TEST(TaskScopeTest, CancellingOneScopeDoesNotCancelAnother)
{
  background_executor_c executor(3U);
  std::latch blocker_active(1);
  std::latch release_blocker(1);
  ASSERT_TRUE(executor.try_post([&]() {
      blocker_active.count_down();
      release_blocker.wait();
  }));
  blocker_active.wait();
  task_scope_c first(executor);
  task_scope_c second(executor);
  std::atomic<bool> first_ran{false};
  std::latch second_ran(1);
  ASSERT_TRUE(first.try_post([&first_ran]() {first_ran = true;}));
  ASSERT_TRUE(second.try_post([&second_ran]() {second_ran.count_down();}));
  first.cancel_and_wait();
  release_blocker.count_down();
  second_ran.wait();
  executor.shutdown(shutdown_mode_e::drain);
  EXPECT_FALSE(first_ran.load());
}

TEST(TaskScopeTest, ConcurrentPostingAndCancellationAreSafe)
{
  background_executor_c executor(64U);
  task_scope_c scope(executor);
  std::latch start(1);
  std::atomic<bool> posting_finished{false};
  std::jthread poster([&]() {
      start.wait();
      while (scope.try_post([]() {})) {
      }
      posting_finished = true;
    });
  start.count_down();
  scope.cancel_and_wait();
  poster.join();
  EXPECT_TRUE(posting_finished.load());
  EXPECT_FALSE(scope.try_post([]() {}));
  executor.shutdown(shutdown_mode_e::cancel_pending);
}

TEST(BackgroundPriorityTest, ClampsNiceValue)
{
  EXPECT_EQ(base_core::execution::detail::calculate_background_nice(-5), -4);
  EXPECT_EQ(base_core::execution::detail::calculate_background_nice(0), 1);
  EXPECT_EQ(base_core::execution::detail::calculate_background_nice(18), 19);
  EXPECT_EQ(base_core::execution::detail::calculate_background_nice(19), 19);
}

}  // namespace
