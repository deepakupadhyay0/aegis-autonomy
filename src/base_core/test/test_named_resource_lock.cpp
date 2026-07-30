#include <gtest/gtest.h>

#include "base_core/resource/named_resource_lock.hpp"

#include <string>
#include <unistd.h>

TEST(NamedResourceLockTest, EnforcesExclusiveProcessOwnership)
{
  const std::string resource_name =
    "@aegis_autonomy.test." + std::to_string(static_cast<int64_t>(::getpid()));
  base_core::resource::named_resource_lock_c first_lock;
  base_core::resource::named_resource_lock_c second_lock;

  ASSERT_EQ(first_lock.try_acquire(resource_name), core_ret_e::ok);
  EXPECT_TRUE(first_lock.owns_lock());
  EXPECT_EQ(first_lock.get_resource_name(), resource_name);
  EXPECT_EQ(first_lock.try_acquire(resource_name), core_ret_e::ok);

  EXPECT_EQ(second_lock.try_acquire(resource_name), core_ret_e::locked);
  EXPECT_FALSE(second_lock.owns_lock());

  first_lock.release();
  EXPECT_FALSE(first_lock.owns_lock());
  EXPECT_EQ(second_lock.try_acquire(resource_name), core_ret_e::ok);
}

TEST(NamedResourceLockTest, RejectsNonAbstractNames)
{
  base_core::resource::named_resource_lock_c resource_lock;

  EXPECT_EQ(resource_lock.try_acquire("camera.front"), core_ret_e::bad_arg);
  EXPECT_FALSE(resource_lock.owns_lock());
}
