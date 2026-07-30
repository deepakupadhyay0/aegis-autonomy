#include "base_core/ipc/ipc_socket.hpp"
#include "common/ipc/ipc_codec.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <utility>

namespace base_core
{
namespace ipc
{

ipc_socket_c::ipc_socket_c(int32_t fd) noexcept
: m_fd(fd)
{
}

ipc_socket_c::~ipc_socket_c() noexcept
{
  this->close_socket();
}

ipc_socket_c::ipc_socket_c(ipc_socket_c && other) noexcept
: m_fd(other.m_fd)
{
  other.m_fd = -1;
}

ipc_socket_c & ipc_socket_c::operator=(ipc_socket_c && other) noexcept
{
  if (this != &other) {
    this->close_socket();
    m_fd = other.m_fd;
    other.m_fd = -1;
  }
  return *this;
}

void ipc_socket_c::close_socket() noexcept
{
  if (m_fd >= 0) {
    ::close(m_fd);
    m_fd = -1;
  }
}

bool8_t ipc_socket_c::is_valid() const noexcept
{
  return (m_fd >= 0);
}

int32_t ipc_socket_c::get_fd() const noexcept
{
  return m_fd;
}

core_ret_t ipc_socket_c::create_socketpair(ipc_socket_c & parent_sock, ipc_socket_c & child_sock) noexcept
{
  int32_t fds[2] = {-1, -1};
  int32_t res = ::socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds);
  if (res < 0) {
    return core_ret_e::error;
  }

  parent_sock.close_socket();
  child_sock.close_socket();

  parent_sock.m_fd = fds[0];
  child_sock.m_fd = fds[1];

  return core_ret_e::ok;
}

core_ret_t ipc_socket_c::bind_and_listen(const std::string & socket_path) noexcept
{
  this->close_socket();
  if (socket_path.empty() || socket_path.size() >= sizeof(sockaddr_un::sun_path)) {
    return core_ret_e::bad_arg;
  }

  m_fd = ::socket(AF_UNIX, SOCK_SEQPACKET, 0);
  if (m_fd < 0) {
    return core_ret_e::error;
  }

  if (socket_path[0] != '@') {
    ::unlink(socket_path.c_str());
  }

  struct sockaddr_un addr = {};
  addr.sun_family = AF_UNIX;
  if (socket_path[0] == '@') {
    addr.sun_path[0] = '\0';
    std::strncpy(addr.sun_path + 1, socket_path.c_str() + 1, sizeof(addr.sun_path) - 2);
  } else {
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
  }

  if (::bind(m_fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
    this->close_socket();
    return core_ret_e::error;
  }

  if (::listen(m_fd, 5) < 0) {
    this->close_socket();
    return core_ret_e::error;
  }

  return core_ret_e::ok;
}

core_ret_t ipc_socket_c::accept_client(ipc_socket_c & client_sock) noexcept
{
  if (m_fd < 0) {
    return core_ret_e::object_is_inactive;
  }

  client_sock.close_socket();
  int32_t cfd = -1;
  do {
    cfd = ::accept(m_fd, nullptr, nullptr);
  } while (cfd < 0 && errno == EINTR);

  if (cfd < 0) {
    return core_ret_e::error;
  }

  client_sock.m_fd = cfd;
  return core_ret_e::ok;
}

core_ret_t ipc_socket_c::connect_to_server(const std::string & socket_path) noexcept
{
  this->close_socket();
  if (socket_path.empty() || socket_path.size() >= sizeof(sockaddr_un::sun_path)) {
    return core_ret_e::bad_arg;
  }

  m_fd = ::socket(AF_UNIX, SOCK_SEQPACKET, 0);
  if (m_fd < 0) {
    return core_ret_e::error;
  }

  struct sockaddr_un addr = {};
  addr.sun_family = AF_UNIX;
  if (socket_path[0] == '@') {
    addr.sun_path[0] = '\0';
    std::strncpy(addr.sun_path + 1, socket_path.c_str() + 1, sizeof(addr.sun_path) - 2);
  } else {
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
  }

  int32_t res = -1;
  do {
    res = ::connect(m_fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
  } while (res < 0 && errno == EINTR);

  if (res < 0) {
    this->close_socket();
    return core_ret_e::error;
  }

  return core_ret_e::ok;
}

core_ret_t ipc_socket_c::send_handshake(int32_t fd_to_send) noexcept
{
  if (m_fd < 0) {
    return core_ret_e::object_is_inactive;
  }
  if (fd_to_send < 0) {
    return core_ret_e::bad_arg;
  }

  std::byte payload{0};
  struct msghdr msgh = {};
  struct iovec iov[1];
  iov[0].iov_base = &payload;
  iov[0].iov_len = sizeof(payload);
  msgh.msg_iov = iov;
  msgh.msg_iovlen = 1;

  char8_t ctrl_buf[CMSG_SPACE(sizeof(int32_t))];
  std::memset(ctrl_buf, 0, sizeof(ctrl_buf));
  msgh.msg_control = ctrl_buf;
  msgh.msg_controllen = sizeof(ctrl_buf);

  struct cmsghdr * const cmsg = CMSG_FIRSTHDR(&msgh);
  if (cmsg == nullptr) {
    return core_ret_e::error;
  }
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(int32_t));
  std::memcpy(CMSG_DATA(cmsg), &fd_to_send, sizeof(int32_t));

  ssize_t bytes_sent = 0;
  do {
    bytes_sent = ::sendmsg(m_fd, &msgh, MSG_NOSIGNAL);
  } while (bytes_sent < 0 && errno == EINTR);

  if (bytes_sent != static_cast<ssize_t>(sizeof(payload))) {
    this->close_socket();
    return core_ret_e::error;
  }
  return core_ret_e::ok;
}

core_ret_t ipc_socket_c::receive_handshake(int32_t & received_fd) noexcept
{
  if (m_fd < 0) {
    return core_ret_e::object_is_inactive;
  }

  received_fd = -1;
  std::byte payload{0};
  struct msghdr msgh = {};
  struct iovec iov[1];
  iov[0].iov_base = &payload;
  iov[0].iov_len = sizeof(payload);
  msgh.msg_iov = iov;
  msgh.msg_iovlen = 1;

  char8_t ctrl_buf[CMSG_SPACE(sizeof(int32_t))];
  std::memset(ctrl_buf, 0, sizeof(ctrl_buf));
  msgh.msg_control = ctrl_buf;
  msgh.msg_controllen = sizeof(ctrl_buf);

  ssize_t bytes_read = 0;
  do {
    bytes_read = ::recvmsg(m_fd, &msgh, MSG_NOSIGNAL);
  } while (bytes_read < 0 && errno == EINTR);

  if (bytes_read != static_cast<ssize_t>(sizeof(payload)) ||
    (msgh.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) != 0)
  {
    this->close_socket();
    return core_ret_e::error;
  }

  const struct cmsghdr * const cmsg = CMSG_FIRSTHDR(&msgh);
  if (cmsg == nullptr || cmsg->cmsg_level != SOL_SOCKET ||
    cmsg->cmsg_type != SCM_RIGHTS || cmsg->cmsg_len < CMSG_LEN(sizeof(int32_t)))
  {
    return core_ret_e::error;
  }
  std::memcpy(&received_fd, CMSG_DATA(cmsg), sizeof(int32_t));
  return received_fd >= 0 ? core_ret_e::ok : core_ret_e::error;
}

core_ret_t ipc_socket_c::send_frame_notification(
  const common::ipc::frame_notification_s & notification) noexcept
{
  if (m_fd < 0) {
    return core_ret_e::object_is_inactive;
  }

  std::array<std::byte, common::ipc::FRAME_NOTIFICATION_SIZE> wire_buffer;
  common::ipc::serialize_frame_notification(notification, wire_buffer);

  ssize_t bytes_sent = 0;
  do {
    bytes_sent = ::send(m_fd, wire_buffer.data(), wire_buffer.size(), MSG_NOSIGNAL);
  } while (bytes_sent < 0 && errno == EINTR);

  if (bytes_sent != static_cast<ssize_t>(wire_buffer.size())) {
    this->close_socket();
    return core_ret_e::error;
  }
  return core_ret_e::ok;
}

core_ret_t ipc_socket_c::receive_frame_notification(
  common::ipc::frame_notification_s & notification) noexcept
{
  if (m_fd < 0) {
    return core_ret_e::object_is_inactive;
  }

  std::array<std::byte, common::ipc::FRAME_NOTIFICATION_SIZE> wire_buffer;
  ssize_t bytes_read = 0;
  do {
    bytes_read = ::recv(m_fd, wire_buffer.data(), wire_buffer.size(), 0);
  } while (bytes_read < 0 && errno == EINTR);

  if (bytes_read != static_cast<ssize_t>(wire_buffer.size())) {
    this->close_socket();
    return core_ret_e::error;
  }
  return common::ipc::deserialize_frame_notification(
    wire_buffer.data(), wire_buffer.size(), notification) ?
    core_ret_e::ok : core_ret_e::bad_arg;
}

} // namespace ipc
} // namespace base_core
