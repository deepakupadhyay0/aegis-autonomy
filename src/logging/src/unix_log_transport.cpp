#include "logging/log_transport.hpp"

#include "common/logging/log_codec.hpp"
#include "common/resource_names.hpp"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <string_view>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace logging
{

namespace
{

template<std::size_t packet_size_v>
bool send_packet(
  const int32_t socket_fd,
  const std::array<std::byte, packet_size_v> & packet) noexcept
{
  ssize_t sent_size = -1;
  do {
    sent_size = ::send(
      socket_fd,
      packet.data(),
      packet.size(),
      MSG_DONTWAIT | MSG_NOSIGNAL);
  } while (sent_size < 0 && errno == EINTR);
  return sent_size == static_cast<ssize_t>(packet.size());
}

}  // namespace

unix_log_transport_c::unix_log_transport_c() noexcept
: m_socket_fd(-1)
{
}

unix_log_transport_c::~unix_log_transport_c() noexcept
{
  this->disconnect();
}

bool unix_log_transport_c::connect(
  const common::logging::log_registration_s & registration) noexcept
{
  this->disconnect();
  m_socket_fd = ::socket(
    AF_UNIX,
    SOCK_SEQPACKET | SOCK_CLOEXEC,
    0);
  if (m_socket_fd < 0) {
    return false;
  }

  const std::string_view endpoint = aegis_autonomy::resources::LOGGING_SERVICE;
  if (endpoint.empty() || endpoint.front() != '@') {
    this->disconnect();
    return false;
  }

  const std::string_view abstract_name = endpoint.substr(1U);
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  address.sun_path[0] = '\0';
  if (abstract_name.size() > sizeof(address.sun_path) - 1U) {
    this->disconnect();
    return false;
  }
  std::memcpy(
    &address.sun_path[1U],
    abstract_name.data(),
    abstract_name.size());
  const socklen_t address_size = static_cast<socklen_t>(
    offsetof(sockaddr_un, sun_path) + 1U + abstract_name.size());

  int32_t connect_result = -1;
  do {
    connect_result = ::connect(
      m_socket_fd,
      reinterpret_cast<const sockaddr *>(&address),
      address_size);
  } while (connect_result < 0 && errno == EINTR);
  if (connect_result < 0) {
    this->disconnect();
    return false;
  }

  const int32_t current_flags = ::fcntl(m_socket_fd, F_GETFL, 0);
  if (current_flags < 0 ||
    ::fcntl(m_socket_fd, F_SETFL, current_flags | O_NONBLOCK) < 0)
  {
    this->disconnect();
    return false;
  }

  std::array<
    std::byte,
    common::logging::LOG_REGISTRATION_WIRE_SIZE> packet{};
  common::logging::serialize_log_registration(registration, packet);
  if (!send_packet(m_socket_fd, packet)) {
    this->disconnect();
    return false;
  }
  return true;
}

bool unix_log_transport_c::send(
  const common::logging::log_record_s & record) noexcept
{
  if (m_socket_fd < 0) {
    return false;
  }

  std::array<std::byte, common::logging::LOG_RECORD_WIRE_SIZE> packet{};
  common::logging::serialize_log_record(record, packet);
  return send_packet(m_socket_fd, packet);
}

void unix_log_transport_c::disconnect() noexcept
{
  if (m_socket_fd >= 0) {
    static_cast<void>(::close(m_socket_fd));
    m_socket_fd = -1;
  }
}

}  // namespace logging
