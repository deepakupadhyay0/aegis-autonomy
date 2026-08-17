#include "base_core/named_resource_lock.hpp"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace base_core
{
namespace resource
{

named_resource_lock_c::named_resource_lock_c() noexcept
: m_socket_fd(-1),
  m_resource_name(),
  m_resource_name_size(0U)
{
}

named_resource_lock_c::~named_resource_lock_c() noexcept
{
  this->release();
}

core_ret_t named_resource_lock_c::try_acquire(
  const std::string_view resource_name) noexcept
{
  if (resource_name.size() <= 1U ||
    resource_name.size() > MAX_RESOURCE_NAME_SIZE ||
    resource_name.front() != '@' ||
    resource_name.find('\0') != std::string_view::npos)
  {
    return core_ret_e::bad_arg;
  }

  if (this->owns_lock()) {
    const bool8_t owns_requested_lock =
      resource_name.size() == m_resource_name_size &&
      std::memcmp(
      resource_name.data(), m_resource_name.data(), m_resource_name_size) == 0;
    return owns_requested_lock ? core_ret_e::ok : core_ret_e::object_is_active;
  }

#if defined(__linux__)
  const int32_t socket_fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (socket_fd < 0) {
    return core_ret_e::error;
  }

  struct sockaddr_un address = {};
  address.sun_family = AF_UNIX;
  address.sun_path[0] = '\0';

  const size_t abstract_name_size = resource_name.size() - 1U;
  std::memcpy(
    &address.sun_path[1], resource_name.data() + 1U, abstract_name_size);

  const socklen_t address_size = static_cast<socklen_t>(
    offsetof(struct sockaddr_un, sun_path) + 1U + abstract_name_size);
  if (::bind(
      socket_fd,
      reinterpret_cast<const struct sockaddr *>(&address),
      address_size) != 0)
  {
    const int32_t bind_error = errno;
    ::close(socket_fd);
    return bind_error == EADDRINUSE ? core_ret_e::locked : core_ret_e::error;
  }

  m_socket_fd = socket_fd;
  std::memcpy(m_resource_name.data(), resource_name.data(), resource_name.size());
  m_resource_name_size = resource_name.size();
  return core_ret_e::ok;
#else
  static_cast<void>(resource_name);
  return core_ret_e::error;
#endif
}

void named_resource_lock_c::release() noexcept
{
  if (m_socket_fd >= 0) {
    const int32_t socket_fd = m_socket_fd;
    m_socket_fd = -1;
    m_resource_name_size = 0U;
    m_resource_name[0U] = '\0';
    ::close(socket_fd);
  }
}

bool8_t named_resource_lock_c::owns_lock() const noexcept
{
  return m_socket_fd >= 0;
}

std::string_view named_resource_lock_c::get_resource_name() const noexcept
{
  return std::string_view(m_resource_name.data(), m_resource_name_size);
}

}  // namespace resource
}  // namespace base_core
