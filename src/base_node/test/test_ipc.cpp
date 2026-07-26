#include <gtest/gtest.h>
#include "base_node/ipc/shm_ring_buffer.hpp"
#include <cstring>

TEST(ShmRingBufferTest, AnonymousShmAllocation)
{
  const uint32_t num_slots = 4U;
  const uint64_t slot_size = 1024U;
  base_node::ipc::shm_ring_buffer_c shm_buf(num_slots, slot_size);

  EXPECT_EQ(shm_buf.get_num_slots(), num_slots);
  EXPECT_EQ(shm_buf.get_slot_size(), slot_size);
  EXPECT_FALSE(shm_buf.is_valid());

  EXPECT_EQ(shm_buf.create_anonymous_shm(), core_ret_e::ok);
  EXPECT_TRUE(shm_buf.is_valid());
  EXPECT_GE(shm_buf.get_shm_fd(), 0);
}

TEST(ShmRingBufferTest, SlotReadWriteVerification)
{
  const uint32_t num_slots = 2U;
  const uint64_t slot_size = 256U;
  base_node::ipc::shm_ring_buffer_c shm_buf(num_slots, slot_size);

  ASSERT_EQ(shm_buf.create_anonymous_shm(), core_ret_e::ok);

  void * slot0 = shm_buf.get_slot_pointer(0);
  void * slot1 = shm_buf.get_slot_pointer(1);

  ASSERT_NE(slot0, nullptr);
  ASSERT_NE(slot1, nullptr);
  EXPECT_NE(slot0, slot1);

  const char * test_str = "Aegis Autonomy IPC Zero-Copy Test";
  std::memcpy(slot0, test_str, std::strlen(test_str) + 1);

  EXPECT_STREQ(static_cast<char *>(slot0), test_str);
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
