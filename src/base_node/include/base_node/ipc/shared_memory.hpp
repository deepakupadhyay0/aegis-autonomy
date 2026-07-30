#pragma once

#include "base_node/core_defs.hpp"
#include "base_node/visibility_control.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace base_node
{
namespace ipc
{

class BASE_NODE_PUBLIC shared_memory_c
{
public:
  shared_memory_c() noexcept;
  ~shared_memory_c() noexcept;

  shared_memory_c(const shared_memory_c &) = delete;
  shared_memory_c & operator=(const shared_memory_c &) = delete;
  shared_memory_c(shared_memory_c &&) = delete;
  shared_memory_c & operator=(shared_memory_c &&) = delete;

  core_ret_t create_anonymous(
    std::string_view name,
    size64_t memory_size) noexcept;

  core_ret_t attach_from_fd(int32_t handle) noexcept;

  void reset() noexcept;

  std::span<std::byte> memory() noexcept;
  std::span<const std::byte> memory() const noexcept;
  size64_t size() const noexcept;
  int32_t handle() const noexcept;
  std::string_view name() const noexcept;
  bool8_t is_valid() const noexcept;

private:
  void * m_memory_pointer;
  size64_t m_memory_size;
  int32_t m_handle;
  common::string256_t m_name;
};

}  // namespace ipc
}  // namespace base_node
