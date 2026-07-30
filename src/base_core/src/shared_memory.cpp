#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "base_core/ipc/shared_memory.hpp"

#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <limits>

namespace base_core
{
namespace ipc
{

namespace
{

constexpr size_t MEMFD_NAME_MAX_SIZE = 249U;

bool8_t is_valid_mapping_size(const size64_t memory_size) noexcept
{
  return memory_size > 0U &&
         memory_size <= static_cast<size64_t>(std::numeric_limits<size_t>::max()) &&
         memory_size <= static_cast<size64_t>(std::numeric_limits<off_t>::max());
}

}  // namespace

shared_memory_c::shared_memory_c() noexcept
: m_memory_pointer(nullptr),
  m_memory_size(0U),
  m_handle(-1),
  m_name()
{
}

shared_memory_c::~shared_memory_c() noexcept
{
  this->reset();
}

core_ret_t shared_memory_c::create_anonymous(
  const std::string_view name,
  const size64_t memory_size) noexcept
{
  this->reset();

  common::string256_t memfd_name;
  if (name.empty() ||
    name.size() > MEMFD_NAME_MAX_SIZE ||
    name.find('\0') != std::string_view::npos ||
    !is_valid_mapping_size(memory_size))
  {
    return core_ret_e::bad_arg;
  }
  memfd_name.assign(name);

  const int32_t handle = ::memfd_create(memfd_name.c_str(), MFD_CLOEXEC);
  if (handle < 0) {
    return core_ret_e::error;
  }

  if (::ftruncate(handle, static_cast<off_t>(memory_size)) != 0) {
    ::close(handle);
    return core_ret_e::error;
  }

  void * const memory_pointer = ::mmap(
    nullptr,
    static_cast<size_t>(memory_size),
    PROT_READ | PROT_WRITE,
    MAP_SHARED,
    handle,
    0);
  if (memory_pointer == MAP_FAILED) {
    ::close(handle);
    return core_ret_e::error;
  }

  m_memory_pointer = memory_pointer;
  m_memory_size = memory_size;
  m_handle = handle;
  m_name = memfd_name;
  return core_ret_e::ok;
}

core_ret_t shared_memory_c::attach_from_fd(const int32_t handle) noexcept
{
  this->reset();

  if (handle < 0) {
    return core_ret_e::bad_arg;
  }

  struct stat file_status {};
  if (::fstat(handle, &file_status) != 0 || file_status.st_size <= 0) {
    ::close(handle);
    return core_ret_e::bad_arg;
  }

  const size64_t memory_size = static_cast<size64_t>(file_status.st_size);
  if (!is_valid_mapping_size(memory_size)) {
    ::close(handle);
    return core_ret_e::too_large;
  }

  void * const memory_pointer = ::mmap(
    nullptr,
    static_cast<size_t>(memory_size),
    PROT_READ | PROT_WRITE,
    MAP_SHARED,
    handle,
    0);
  if (memory_pointer == MAP_FAILED) {
    ::close(handle);
    return core_ret_e::error;
  }

  m_memory_pointer = memory_pointer;
  m_memory_size = memory_size;
  m_handle = handle;
  return core_ret_e::ok;
}

void shared_memory_c::reset() noexcept
{
  if (m_memory_pointer != nullptr) {
    (void)::munmap(m_memory_pointer, static_cast<size_t>(m_memory_size));
  }
  if (m_handle >= 0) {
    (void)::close(m_handle);
  }

  m_memory_pointer = nullptr;
  m_memory_size = 0U;
  m_handle = -1;
  m_name.clear();
}

std::span<std::byte> shared_memory_c::memory() noexcept
{
  return {
    static_cast<std::byte *>(m_memory_pointer),
    static_cast<size_t>(m_memory_size)};
}

std::span<const std::byte> shared_memory_c::memory() const noexcept
{
  return {
    static_cast<const std::byte *>(m_memory_pointer),
    static_cast<size_t>(m_memory_size)};
}

size64_t shared_memory_c::size() const noexcept
{
  return m_memory_size;
}

int32_t shared_memory_c::handle() const noexcept
{
  return m_handle;
}

std::string_view shared_memory_c::name() const noexcept
{
  return m_name.view();
}

bool8_t shared_memory_c::is_valid() const noexcept
{
  return m_memory_pointer != nullptr && m_memory_size > 0U && m_handle >= 0;
}

}  // namespace ipc
}  // namespace base_core
