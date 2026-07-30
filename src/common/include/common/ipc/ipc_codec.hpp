#pragma once

#include "common/ipc/ipc_protocol.hpp"

#include <array>
#include <cstddef>

namespace common
{
namespace ipc
{

constexpr size_t STREAM_DESCRIPTOR_SIZE = 64U;
constexpr size_t FRAME_NOTIFICATION_SIZE = 20U;

void serialize_stream_descriptor(
  const stream_descriptor_s & descriptor,
  std::array<std::byte, STREAM_DESCRIPTOR_SIZE> & buffer) noexcept;

bool deserialize_stream_descriptor(
  const std::byte * buffer,
  size_t buffer_size,
  stream_descriptor_s & descriptor) noexcept;

void serialize_frame_notification(
  const frame_notification_s & notification,
  std::array<std::byte, FRAME_NOTIFICATION_SIZE> & buffer) noexcept;

bool deserialize_frame_notification(
  const std::byte * buffer,
  size_t buffer_size,
  frame_notification_s & notification) noexcept;

}  // namespace ipc
}  // namespace common
