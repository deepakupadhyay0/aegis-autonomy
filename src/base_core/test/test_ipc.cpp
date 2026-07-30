#include <gtest/gtest.h>
#include "base_core/ipc/shm_ring_buffer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <unistd.h>

TEST(ShmRingBufferTest, AnonymousShmAllocation)
{
  const uint32_t num_slots = 4U;
  const uint64_t slot_size = 640U * 3U;
  const std::array<std::byte, 4U> metadata{
    std::byte{0x01U},
    std::byte{0x02U},
    std::byte{0x03U},
    std::byte{0x04U}};
  base_core::ipc::shm_ring_buffer_c shm_buf(num_slots, slot_size);

  EXPECT_EQ(shm_buf.get_num_slots(), num_slots);
  EXPECT_EQ(shm_buf.get_slot_size(), slot_size);
  EXPECT_FALSE(shm_buf.is_valid());

  EXPECT_EQ(
    shm_buf.create_anonymous_shm("@aegis_autonomy.test.shm", metadata),
    core_ret_e::ok);
  EXPECT_TRUE(shm_buf.is_valid());
  EXPECT_GE(shm_buf.get_shm_fd(), 0);
  EXPECT_TRUE(std::equal(
    metadata.begin(),
    metadata.end(),
    shm_buf.get_metadata().begin()));
}

TEST(ShmRingBufferTest, SlotReadWriteVerification)
{
  const uint32_t num_slots = 2U;
  const uint64_t slot_size = 64U * 3U;
  const std::array<std::byte, 0U> metadata{};
  base_core::ipc::shm_ring_buffer_c shm_buf(num_slots, slot_size);

  ASSERT_EQ(
    shm_buf.create_anonymous_shm("@aegis_autonomy.test.shm", metadata),
    core_ret_e::ok);

  void * slot0 = shm_buf.get_slot_pointer(0);
  void * slot1 = shm_buf.get_slot_pointer(1);

  ASSERT_NE(slot0, nullptr);
  ASSERT_NE(slot1, nullptr);
  EXPECT_NE(slot0, slot1);

  const char * test_str = "Aegis Autonomy IPC Zero-Copy Test";
  std::memcpy(slot0, test_str, std::strlen(test_str) + 1);

  EXPECT_STREQ(static_cast<char *>(slot0), test_str);
}

TEST(ShmRingBufferTest, AttachReadsGenericLayoutAndMetadata)
{
  const std::array<std::byte, 3U> metadata{
    std::byte{0x0AU},
    std::byte{0x0BU},
    std::byte{0x0CU}};
  base_core::ipc::shm_ring_buffer_c producer(2U, 128U);
  ASSERT_EQ(
    producer.create_anonymous_shm("@aegis_autonomy.test.attach", metadata),
    core_ret_e::ok);

  const int32_t consumer_fd = ::dup(producer.get_shm_fd());
  ASSERT_GE(consumer_fd, 0);

  base_core::ipc::shm_ring_buffer_c consumer;
  ASSERT_EQ(consumer.attach_from_fd(consumer_fd), core_ret_e::ok);
  EXPECT_EQ(consumer.get_num_slots(), producer.get_num_slots());
  EXPECT_EQ(consumer.get_slot_size(), producer.get_slot_size());
  ASSERT_EQ(consumer.get_metadata().size(), metadata.size());
  EXPECT_TRUE(std::equal(
    metadata.begin(),
    metadata.end(),
    consumer.get_metadata().begin()));
}

TEST(ShmRingBufferTest, ExplicitSlotOwnership)
{
  const std::array<std::byte, 0U> metadata{};
  base_core::ipc::shm_ring_buffer_c shm_buf(1U, 64U * 3U);
  ASSERT_EQ(
    shm_buf.create_anonymous_shm("@aegis_autonomy.test.shm", metadata),
    core_ret_e::ok);

  uint32_t slot_index = 0U;
  ASSERT_TRUE(shm_buf.try_acquire_slot_for_write(1U, slot_index));
  EXPECT_FALSE(shm_buf.try_acquire_slot_for_read(slot_index, 1U));
  ASSERT_EQ(shm_buf.publish_written_slot(slot_index, 1U), core_ret_e::ok);
  EXPECT_FALSE(shm_buf.try_acquire_slot_for_read(slot_index, 2U));
  ASSERT_TRUE(shm_buf.try_acquire_slot_for_read(slot_index, 1U));

  uint32_t unavailable_slot = 0U;
  EXPECT_FALSE(shm_buf.try_acquire_slot_for_write(2U, unavailable_slot));
  ASSERT_EQ(shm_buf.release_read_slot(slot_index, 1U), core_ret_e::ok);
  EXPECT_TRUE(shm_buf.try_acquire_slot_for_write(2U, unavailable_slot));
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
