#pragma once

#include "base_node/core_defs.hpp"
#include "base_node/visibility_control.hpp"
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <utility>

namespace base_node
{
namespace ipc
{

struct ipc_message_t
{
  uint32_t msg_id;
  uint32_t slot_index;
  uint32_t width;
  uint32_t height;
  uint32_t format; // e.g. 1 = BGR8
  uint64_t timestamp_ns;
};

class BASE_NODE_PUBLIC ipc_socket_c
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

  static core_ret_t create_socketpair(ipc_socket_c & parent_sock, ipc_socket_c & child_sock) noexcept;
  core_ret_t bind_and_listen(const std::string & socket_path) noexcept;
  core_ret_t accept_client(ipc_socket_c & client_sock) noexcept;
  core_ret_t connect_to_server(const std::string & socket_path) noexcept;

  core_ret_t send_message(const ipc_message_t & msg, int32_t fd_to_send = -1) noexcept;
  core_ret_t receive_message(ipc_message_t & msg, int32_t * received_fd = nullptr) noexcept;

  void close_socket() noexcept;
  bool8_t is_valid() const noexcept;
  int32_t get_fd() const noexcept;

private:
  int32_t m_fd;
};

} // namespace ipc
} // namespace base_node
