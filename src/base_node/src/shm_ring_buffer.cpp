#include "base_node/ipc/shm_ring_buffer.hpp"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <string>

namespace base_node
{
namespace ipc
{

shm_ring_buffer_c::shm_ring_buffer_c(uint32_t num_slots, size64_t slot_size) noexcept
: m_num_slots(num_slots),
  m_slot_size(slot_size),
  m_total_size(static_cast<size64_t>(num_slots) * slot_size),
  m_shm_fd(-1),
  m_mapped_ptr(MAP_FAILED),
  m_is_owner(false)
{
}

shm_ring_buffer_c::~shm_ring_buffer_c() noexcept
{
  this->cleanup();
}

void shm_ring_buffer_c::cleanup() noexcept
{
  if (m_mapped_ptr != MAP_FAILED && m_mapped_ptr != nullptr) {
    ::munmap(m_mapped_ptr, static_cast<size_t>(m_total_size));
    m_mapped_ptr = MAP_FAILED;
  }
  if (m_shm_fd >= 0) {
    ::close(m_shm_fd);
    m_shm_fd = -1;
  }
  m_is_owner = false;
}

core_ret_t shm_ring_buffer_c::create_anonymous_shm() noexcept
{
  this->cleanup();

  const std::string shm_name = "/shm_ring_" + std::to_string(::getpid());
  m_shm_fd = ::shm_open(shm_name.c_str(), O_CREAT | O_RDWR | O_EXCL, 0600);
  if (m_shm_fd < 0) {
    return core_ret_e::error;
  }

  // Immediately unlink so it becomes anonymous on the filesystem and cannot be sniffed by path
  ::shm_unlink(shm_name.c_str());

  if (::ftruncate(m_shm_fd, static_cast<off_t>(m_total_size)) < 0) {
    this->cleanup();
    return core_ret_e::error;
  }

  m_mapped_ptr = ::mmap(nullptr, static_cast<size_t>(m_total_size), PROT_READ | PROT_WRITE, MAP_SHARED, m_shm_fd, 0);
  if (m_mapped_ptr == MAP_FAILED) {
    this->cleanup();
    return core_ret_e::error;
  }

  std::memset(m_mapped_ptr, 0, static_cast<size_t>(m_total_size));
  m_is_owner = true;

  return core_ret_e::ok;
}

core_ret_t shm_ring_buffer_c::attach_from_fd(int32_t fd) noexcept
{
  this->cleanup();

  if (fd < 0) {
    return core_ret_e::bad_arg;
  }

  m_shm_fd = fd;
  m_mapped_ptr = ::mmap(nullptr, static_cast<size_t>(m_total_size), PROT_READ | PROT_WRITE, MAP_SHARED, m_shm_fd, 0);
  if (m_mapped_ptr == MAP_FAILED) {
    m_shm_fd = -1; // Don't close caller's fd if mmap failed
    return core_ret_e::error;
  }

  m_is_owner = false;
  return core_ret_e::ok;
}

void * shm_ring_buffer_c::get_slot_pointer(uint32_t slot_index) const noexcept
{
  if (slot_index >= m_num_slots || !this->is_valid()) {
    return nullptr;
  }
  uint8_t * base_ptr = static_cast<uint8_t *>(m_mapped_ptr);
  return static_cast<void *>(base_ptr + (static_cast<size_t>(slot_index) * static_cast<size_t>(m_slot_size)));
}

uint32_t shm_ring_buffer_c::get_num_slots() const noexcept
{
  return m_num_slots;
}

size64_t shm_ring_buffer_c::get_slot_size() const noexcept
{
  return m_slot_size;
}

int32_t shm_ring_buffer_c::get_shm_fd() const noexcept
{
  return m_shm_fd;
}

bool8_t shm_ring_buffer_c::is_valid() const noexcept
{
  return (m_mapped_ptr != MAP_FAILED && m_mapped_ptr != nullptr && m_shm_fd >= 0);
}

} // namespace ipc
} // namespace base_node
