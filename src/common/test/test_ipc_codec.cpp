#include <gtest/gtest.h>
#include "common/ipc/ipc_codec.hpp"

#include <array>
#include <cstddef>

TEST(IpcCodecTest, FrameNotificationRoundTrip)
{
  common::ipc::frame_notification_s source;
  source.sequence = 42U;
  source.timestamp_ns = 123456U;
  source.slot_index = 2U;

  std::array<std::byte, common::ipc::FRAME_NOTIFICATION_SIZE> wire_buffer;
  common::ipc::serialize_frame_notification(source, wire_buffer);

  common::ipc::frame_notification_s decoded;
  ASSERT_TRUE(common::ipc::deserialize_frame_notification(
    wire_buffer.data(), wire_buffer.size(), decoded));
  EXPECT_EQ(decoded.sequence, source.sequence);
  EXPECT_EQ(decoded.timestamp_ns, source.timestamp_ns);
  EXPECT_EQ(decoded.slot_index, source.slot_index);
}

TEST(IpcCodecTest, RejectsInvalidNotificationSize)
{
  std::array<std::byte, common::ipc::FRAME_NOTIFICATION_SIZE> wire_buffer;
  common::ipc::frame_notification_s decoded;
  EXPECT_FALSE(common::ipc::deserialize_frame_notification(
    wire_buffer.data(), wire_buffer.size() - 1U, decoded));
}

TEST(IpcCodecTest, StreamDescriptorRoundTrip)
{
  common::ipc::stream_descriptor_s source;
  source.num_slots = 3U;
  source.slot_size = 640U * 480U * 3U;
  source.width = 640U;
  source.height = 480U;
  source.stride = 640U * 3U;
  source.format = common::ipc::pixel_format_e::bgr8;

  std::array<std::byte, common::ipc::STREAM_DESCRIPTOR_SIZE> wire_buffer;
  common::ipc::serialize_stream_descriptor(source, wire_buffer);

  common::ipc::stream_descriptor_s decoded;
  ASSERT_TRUE(common::ipc::deserialize_stream_descriptor(
    wire_buffer.data(), wire_buffer.size(), decoded));
  EXPECT_EQ(decoded.slot_size, source.slot_size);
  EXPECT_EQ(decoded.num_slots, source.num_slots);
  EXPECT_EQ(decoded.width, source.width);
  EXPECT_EQ(decoded.height, source.height);
  EXPECT_EQ(decoded.stride, source.stride);
  EXPECT_EQ(decoded.format, source.format);
}
