#pragma once

#include "base_core/core_defs.hpp"
#include "base_core/ipc/shared_memory.hpp"
#include "base_core/visibility_control.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace base_core
{
namespace ipc
{

class BASE_CORE_PUBLIC shm_ring_buffer_c
{
public:
  shm_ring_buffer_c() noexcept;
  shm_ring_buffer_c(uint32_t num_slots, size64_t slot_size) noexcept;
  ~shm_ring_buffer_c() noexcept;

  shm_ring_buffer_c(const shm_ring_buffer_c &) = delete;
  shm_ring_buffer_c & operator=(const shm_ring_buffer_c &) = delete;
  shm_ring_buffer_c(shm_ring_buffer_c &&) = delete;
  shm_ring_buffer_c & operator=(shm_ring_buffer_c &&) = delete;

  core_ret_t create_anonymous_shm(
    std::string_view name,
    std::span<const std::byte> metadata) noexcept;

  core_ret_t attach_from_fd(int32_t fd) noexcept;

  void * get_slot_pointer(uint32_t slot_index) const noexcept;
  std::span<const std::byte> get_metadata() const noexcept;
  bool8_t try_acquire_slot_for_write(uint64_t sequence, uint32_t & slot_index) noexcept;
  core_ret_t publish_written_slot(uint32_t slot_index, uint64_t sequence) noexcept;
  bool8_t try_acquire_slot_for_read(uint32_t slot_index, uint64_t sequence) noexcept;
  core_ret_t release_read_slot(uint32_t slot_index, uint64_t sequence) noexcept;
  uint32_t get_num_slots() const noexcept;
  size64_t get_slot_size() const noexcept;
  int32_t get_shm_fd() const noexcept;
  bool8_t is_valid() const noexcept;

private:
  uint64_t * get_slot_control_pointer(uint32_t slot_index) const noexcept;

  shared_memory_c m_shared_memory;
  uint32_t m_num_slots;
  size64_t m_slot_size;
  uint32_t m_metadata_size;
  size64_t m_control_offset;
  size64_t m_data_offset;
};

}  // namespace ipc
}  // namespace base_core
