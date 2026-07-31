#include "common/process/exit_detection.hpp"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>

namespace common
{
namespace process
{

int32_t register_exit_handler()
{
  struct exit_handler_state_s
  {
    volatile std::sig_atomic_t write_fd{-1};
    int32_t read_fd{-1};
    std::array<int32_t, 3U> signals{SIGINT, SIGTERM, SIGHUP};
    std::array<struct sigaction, 3U> previous_actions{};
    std::size_t installed_signal_count{0U};

    ~exit_handler_state_s() noexcept
    {
      this->reset();
    }

    void reset() noexcept
    {
      const int32_t write_fd_copy = static_cast<int32_t>(write_fd);
      write_fd = -1;

      for (std::size_t index = 0U; index < installed_signal_count; ++index) {
        static_cast<void>(::sigaction(
            signals[index],
            &previous_actions[index],
            nullptr));
      }
      installed_signal_count = 0U;

      if (read_fd >= 0) {
        static_cast<void>(::close(read_fd));
        read_fd = -1;
      }
      if (write_fd_copy >= 0) {
        static_cast<void>(::close(write_fd_copy));
      }
    }
  };

  static exit_handler_state_s state;

  if (state.write_fd >= 0) {
    throw std::logic_error("An exit handler is already registered");
  }

  {
    std::array<int32_t, 2U> notification_fds{-1, -1};
    if (::pipe2(notification_fds.data(), O_CLOEXEC | O_NONBLOCK) != 0) {
      throw std::runtime_error("Failed to create exit notification pipe");
    }

    state.read_fd = notification_fds[0U];
    state.write_fd = notification_fds[1U];
  }

  struct sigaction action {};
  action.sa_handler = [](const int32_t signal_number) noexcept {
      const int32_t saved_errno = errno;
      const int32_t write_fd = static_cast<int32_t>(state.write_fd);
      if (write_fd >= 0) {
        const exit_notification_s notification{signal_number};
        static_cast<void>(::write(
            write_fd,
            &notification,
            sizeof(notification)));
      }
      errno = saved_errno;
    };
  static_cast<void>(::sigemptyset(&action.sa_mask));
  action.sa_flags = 0;

  for (std::size_t index = 0U; index < state.signals.size(); ++index) {
    if (::sigaction(
        state.signals[index],
        &action,
        &state.previous_actions[index]) != 0)
    {
      state.reset();
      throw std::runtime_error("Failed to register exit signal handler");
    }
    ++state.installed_signal_count;
  }
  return state.read_fd;
}

}  // namespace process
}  // namespace common
