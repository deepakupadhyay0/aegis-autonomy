#include <gtest/gtest.h>

#include "base_node/mutex.hpp"

#include <chrono>
#include <thread>

TEST(MutexTest, TracksNativeLockState)
{
  base_node::sync::mutex_c mutex(
    base_node::sync::priority_inheritance_e::disabled);
  EXPECT_FALSE(mutex.is_locked());

  mutex.lock();
  EXPECT_TRUE(mutex.is_locked());

  mutex.unlock();
  EXPECT_FALSE(mutex.is_locked());
}

TEST(MutexTest, TimedLockReportsTimeout)
{
  base_node::sync::mutex_c mutex(
    base_node::sync::priority_inheritance_e::disabled);
  mutex.lock();

  core_ret_e result = core_ret_e::error;
  std::thread contender([&mutex, &result]() {
    result = mutex.timedlock_ms(10);
  });
  contender.join();

  EXPECT_EQ(result, core_ret_e::timeout);
  mutex.unlock();
}
