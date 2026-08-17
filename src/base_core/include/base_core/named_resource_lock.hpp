#pragma once

#include "base_core/core_defs.hpp"
#include "base_core/visibility_control.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace base_core
{
namespace resource
{

/// @brief Process-level ownership lock backed by a Linux abstract Unix socket.
/// Not thread-safe. Acquire once during initialization and hold until shutdown.
class BASE_CORE_PUBLIC named_resource_lock_c final
{
public:
  named_resource_lock_c() noexcept;
  ~named_resource_lock_c() noexcept;

  named_resource_lock_c(const named_resource_lock_c &) = delete;
  named_resource_lock_c & operator=(const named_resource_lock_c &) = delete;
  named_resource_lock_c(named_resource_lock_c &&) = delete;
  named_resource_lock_c & operator=(named_resource_lock_c &&) = delete;

  core_ret_t try_acquire(std::string_view resource_name) noexcept;
  void release() noexcept;

  bool8_t owns_lock() const noexcept;
  std::string_view get_resource_name() const noexcept;

private:
  static constexpr size_t MAX_RESOURCE_NAME_SIZE = 108U;

  int32_t m_socket_fd;
  std::array<char, MAX_RESOURCE_NAME_SIZE> m_resource_name;
  size_t m_resource_name_size;
};

}  // namespace resource
}  // namespace base_core
