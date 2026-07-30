#include "presence_detection/ipc/camera_ipc_validation.hpp"

#include <cstring>
#include <limits>

namespace presence_detection
{
namespace ipc
{

namespace
{

common::uint32_t bytes_per_pixel(
  const common::ipc::pixel_format_e format) noexcept
{
  if (format == common::ipc::pixel_format_e::bgr8 ||
    format == common::ipc::pixel_format_e::rgb8)
  {
    return 3U;
  }
  if (format == common::ipc::pixel_format_e::mono8) {
    return 1U;
  }
  return 0U;
}

bool get_packed_stride(
  const common::ipc::stream_descriptor_s & descriptor,
  common::uint32_t & packed_stride) noexcept
{
  const common::uint32_t pixel_size = bytes_per_pixel(descriptor.format);
  if (descriptor.width == 0U ||
    pixel_size == 0U ||
    descriptor.width >
    (std::numeric_limits<common::uint32_t>::max() / pixel_size))
  {
    return false;
  }

  packed_stride = descriptor.width * pixel_size;
  return true;
}

}  // namespace

bool validate_camera_stream_descriptor(
  const common::ipc::stream_descriptor_s & descriptor) noexcept
{
  const common::uint32_t pixel_size = bytes_per_pixel(descriptor.format);
  if (descriptor.num_slots == 0U || descriptor.slot_size == 0U ||
    descriptor.width == 0U || descriptor.height == 0U || pixel_size == 0U)
  {
    return false;
  }
  if (descriptor.width >
    (std::numeric_limits<common::uint32_t>::max() / pixel_size))
  {
    return false;
  }

  const common::uint32_t minimum_stride = descriptor.width * pixel_size;
  return descriptor.stride >= minimum_stride &&
         static_cast<common::uint64_t>(descriptor.stride) *
         descriptor.height <=
         descriptor.slot_size;
}

bool make_camera_stream_descriptor(
  const cv::Mat & frame,
  const common::uint32_t num_slots,
  common::ipc::stream_descriptor_s & descriptor) noexcept
{
  descriptor = common::ipc::stream_descriptor_s{};
  if (frame.empty() ||
    frame.type() != CV_8UC3 ||
    frame.cols <= 0 ||
    frame.rows <= 0 ||
    num_slots == 0U)
  {
    return false;
  }

  const common::uint64_t width =
    static_cast<common::uint64_t>(frame.cols);
  const common::uint64_t height =
    static_cast<common::uint64_t>(frame.rows);
  const common::uint64_t packed_stride = width * 3U;
  if (packed_stride > std::numeric_limits<common::uint32_t>::max() ||
    height >
    (std::numeric_limits<common::uint64_t>::max() / packed_stride))
  {
    return false;
  }

  descriptor.slot_size = packed_stride * height;
  descriptor.num_slots = num_slots;
  descriptor.width = static_cast<common::uint32_t>(width);
  descriptor.height = static_cast<common::uint32_t>(height);
  descriptor.stride = static_cast<common::uint32_t>(packed_stride);
  descriptor.format = common::ipc::pixel_format_e::bgr8;
  return validate_camera_stream_descriptor(descriptor);
}

bool copy_camera_frame_to_buffer(
  const cv::Mat & frame,
  const common::ipc::stream_descriptor_s & descriptor,
  const std::span<std::byte> destination) noexcept
{
  if (!is_camera_frame_compatible(frame, descriptor) ||
    descriptor.slot_size > destination.size())
  {
    return false;
  }

  if (frame.isContinuous() && frame.step[0U] == descriptor.stride) {
    std::memcpy(
      destination.data(),
      frame.data,
      static_cast<std::size_t>(descriptor.slot_size));
    return true;
  }

  for (common::uint32_t row = 0U; row < descriptor.height; ++row) {
    std::memcpy(
      destination.data() +
      (static_cast<std::size_t>(row) * descriptor.stride),
      frame.ptr(static_cast<common::int32_t>(row)),
      descriptor.stride);
  }
  return true;
}

bool is_camera_frame_compatible(
  const cv::Mat & frame,
  const common::ipc::stream_descriptor_s & descriptor) noexcept
{
  common::uint32_t packed_stride = 0U;
  return validate_camera_stream_descriptor(descriptor) &&
         get_packed_stride(descriptor, packed_stride) &&
         descriptor.format == common::ipc::pixel_format_e::bgr8 &&
         descriptor.stride == packed_stride &&
         frame.type() == CV_8UC3 &&
         frame.cols == static_cast<common::int32_t>(descriptor.width) &&
         frame.rows == static_cast<common::int32_t>(descriptor.height) &&
         frame.step[0U] >= packed_stride;
}

}  // namespace ipc
}  // namespace presence_detection
