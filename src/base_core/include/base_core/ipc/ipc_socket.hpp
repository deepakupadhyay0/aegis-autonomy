#pragma once

#include "base_core/core_defs.hpp"
#include "base_core/visibility_control.hpp"
#include "common/ipc/ipc_protocol.hpp"
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <utility>

namespace base_core
{
namespace ipc
{

class BASE_CORE_PUBLIC ipc_socket_c
{
public:
  explicit ipc_socket_c(int32_t fd = -1) noexcept;
  ~ipc_socket_c() noexcept;

  // Disable copy construction and copy assignment
  ipc_socket_c(const ipc_socket_c &) = delete;
  ipc_socket_c & operator=(const ipc_socket_c &) = delete;

  // Enable move construction and move assignment
  ipc_socket_c(ipc_socket_c && other) noexcept;
  ipc_socket_c & operator=(ipc_socket_c && other) noexcept;

  static core_ret_t create_socketpair(
    ipc_socket_c & parent_sock,
    ipc_socket_c & child_sock) noexcept;
  core_ret_t bind_and_listen(const std::string & socket_path) noexcept;
  core_ret_t accept_client(ipc_socket_c & client_sock) noexcept;
  core_ret_t connect_to_server(const std::string & socket_path) noexcept;

  core_ret_t send_handshake(int32_t fd_to_send) noexcept;
  core_ret_t receive_handshake(int32_t & received_fd) noexcept;
  core_ret_t send_frame_notification(
    const common::ipc::frame_notification_s & notification) noexcept;
  core_ret_t receive_frame_notification(
    common::ipc::frame_notification_s & notification) noexcept;

  void close_socket() noexcept;
  bool8_t is_valid() const noexcept;
  int32_t get_fd() const noexcept;

private:
  int32_t m_fd;
};

} // namespace ipc
} // namespace base_core
