#include "common/ipc/ipc_codec.hpp"

#include <cstring>

namespace common
{
namespace ipc
{

namespace
{

template<typename value_t>
void write_value(std::byte * const destination, const value_t value) noexcept
{
  std::memcpy(destination, &value, sizeof(value));
}

template<typename value_t>
value_t read_value(const std::byte * const source) noexcept
{
  value_t value{};
  std::memcpy(&value, source, sizeof(value));
  return value;
}

}  // namespace

void serialize_stream_descriptor(
  const stream_descriptor_s & descriptor,
  std::array<std::byte, STREAM_DESCRIPTOR_SIZE> & buffer) noexcept
{
  buffer.fill(std::byte{0});
  write_value<uint64_t>(&buffer[0U], descriptor.slot_size);
  write_value<uint32_t>(&buffer[8U], descriptor.num_slots);
  write_value<uint32_t>(&buffer[12U], descriptor.width);
  write_value<uint32_t>(&buffer[16U], descriptor.height);
  write_value<uint32_t>(&buffer[20U], descriptor.stride);
  write_value<uint32_t>(&buffer[24U], static_cast<uint32_t>(descriptor.format));
}

bool deserialize_stream_descriptor(
  const std::byte * const buffer,
  const size_t buffer_size,
  stream_descriptor_s & descriptor) noexcept
{
  if (buffer == nullptr || buffer_size < STREAM_DESCRIPTOR_SIZE) {
    return false;
  }

  descriptor.slot_size = read_value<uint64_t>(&buffer[0U]);
  descriptor.num_slots = read_value<uint32_t>(&buffer[8U]);
  descriptor.width = read_value<uint32_t>(&buffer[12U]);
  descriptor.height = read_value<uint32_t>(&buffer[16U]);
  descriptor.stride = read_value<uint32_t>(&buffer[20U]);
  descriptor.format = static_cast<pixel_format_e>(read_value<uint32_t>(&buffer[24U]));
  return true;
}

void serialize_frame_notification(
  const frame_notification_s & notification,
  std::array<std::byte, FRAME_NOTIFICATION_SIZE> & buffer) noexcept
{
  write_value<uint64_t>(&buffer[0U], notification.sequence);
  write_value<uint64_t>(&buffer[8U], notification.timestamp_ns);
  write_value<uint32_t>(&buffer[16U], notification.slot_index);
}

bool deserialize_frame_notification(
  const std::byte * const buffer,
  const size_t buffer_size,
  frame_notification_s & notification) noexcept
{
  if (buffer == nullptr || buffer_size != FRAME_NOTIFICATION_SIZE) {
    return false;
  }

  notification.sequence = read_value<uint64_t>(&buffer[0U]);
  notification.timestamp_ns = read_value<uint64_t>(&buffer[8U]);
  notification.slot_index = read_value<uint32_t>(&buffer[16U]);
  return true;
}

}  // namespace ipc
}  // namespace common
