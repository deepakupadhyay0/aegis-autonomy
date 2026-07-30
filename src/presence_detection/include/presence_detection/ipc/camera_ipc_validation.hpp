#pragma once

#include "common/ipc/ipc_protocol.hpp"
#include "common/numeric_types.hpp"

#include <opencv2/core/mat.hpp>

#include <cstddef>
#include <span>

namespace presence_detection
{
namespace ipc
{

bool validate_camera_stream_descriptor(
  const common::ipc::stream_descriptor_s & descriptor) noexcept;

bool make_camera_stream_descriptor(
  const cv::Mat & frame,
  common::uint32_t num_slots,
  common::ipc::stream_descriptor_s & descriptor) noexcept;

bool is_camera_frame_compatible(
  const cv::Mat & frame,
  const common::ipc::stream_descriptor_s & descriptor) noexcept;

bool copy_camera_frame_to_buffer(
  const cv::Mat & frame,
  const common::ipc::stream_descriptor_s & descriptor,
  std::span<std::byte> destination) noexcept;

}  // namespace ipc
}  // namespace presence_detection
