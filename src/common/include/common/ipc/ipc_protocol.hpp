#pragma once

#include <cstdint>

namespace common
{
namespace ipc
{

// The explicit width is part of the shared-memory protocol layout.
enum class pixel_format_e : uint32_t  // NOLINT(performance-enum-size)
{
  unknown = 0U,
  bgr8 = 1U,
  rgb8 = 2U,
  mono8 = 3U
};

struct stream_descriptor_s
{
  uint64_t slot_size{0U};
  uint32_t num_slots{0U};
  uint32_t width{0U};
  uint32_t height{0U};
  uint32_t stride{0U};
  pixel_format_e format{pixel_format_e::unknown};
};

struct frame_notification_s
{
  uint64_t sequence{0U};
  uint64_t timestamp_ns{0U};
  uint32_t slot_index{0U};
};

}  // namespace ipc
}  // namespace common
