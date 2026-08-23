#include <gtest/gtest.h>

#include "base_core/mutex.hpp"

#include <cerrno>
#include <chrono>
#include <system_error>
#include <thread>

TEST(MutexTest, TracksNativeLockState)
{
  base_core::sync::mutex_c mutex(
    base_core::sync::priority_inheritance_e::disabled);
  EXPECT_FALSE(mutex.is_locked());

  mutex.lock();
  EXPECT_TRUE(mutex.is_locked());

  mutex.unlock();
  EXPECT_FALSE(mutex.is_locked());
}

TEST(MutexTest, TimedLockReportsTimeout)
{
  base_core::sync::mutex_c mutex(
    base_core::sync::priority_inheritance_e::disabled);
  mutex.lock();

  core_ret_e result = core_ret_e::error;
  std::thread contender([&mutex, &result]() {
      result = mutex.timedlock_ms(10);
    });
  contender.join();

  EXPECT_EQ(result, core_ret_e::timeout);
  mutex.unlock();
}

TEST(TimedMutexTest, ThrowsWhenCurrentAcquisitionRequestTimesOut)
{
  using namespace std::chrono_literals;

  base_core::sync::timed_mutex_c mutex(
    base_core::sync::priority_inheritance_e::disabled,
    1ms);
  mutex.lock();

  std::error_code contender_error;
  std::thread contender([&mutex, &contender_error]() {
      try {
        mutex.lock();
      } catch (const std::system_error & error) {
        contender_error = error.code();
      }
    });
  contender.join();

  EXPECT_EQ(contender_error.value(), ETIMEDOUT);
  mutex.unlock();
}
