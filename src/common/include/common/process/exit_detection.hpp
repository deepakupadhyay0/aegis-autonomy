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
/// @throws std::logic_error when an exit handler is already registered.
/// @note Handles SIGINT, SIGTERM, and SIGHUP.
[[nodiscard]] int32_t register_exit_handler();

}  // namespace process
}  // namespace common
