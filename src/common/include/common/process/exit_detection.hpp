#pragma once

#include <cstdint>

namespace common
{
namespace process
{

struct exit_notification_s
{
  int32_t signal_number{0};
};

/// @brief Creates a process-wide self-pipe for graceful exit notifications.
/// @return The process-lifetime pipe read descriptor.
[[nodiscard]] int32_t register_exit_handler();

}  // namespace process
}  // namespace common
