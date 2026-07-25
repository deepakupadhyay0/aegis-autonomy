#pragma once

#include "base_node/core_defs.hpp"
#include "base_node/visibility_control.hpp"
#include "base_node/ipc/raw_buffer_base.hpp"
#include <cstdint>
#include <cstddef>
#include <string>

namespace base_node
{
namespace ipc
{

class BASE_NODE_PUBLIC shm_ring_buffer_c : public raw_buffer_base_c
{
public:
  explicit shm_ring_buffer_c(uint32_t num_slots, size64_t slot_size) noexcept;
  ~shm_ring_buffer_c() override;

  // Disable copy and move construction/assignment for memory mapped pointer safety
  shm_ring_buffer_c(const shm_ring_buffer_c &) = delete;
  shm_ring_buffer_c & operator=(const shm_ring_buffer_c &) = delete;
  shm_ring_buffer_c(shm_ring_buffer_c &&) = delete;
  shm_ring_buffer_c & operator=(shm_ring_buffer_c &&) = delete;

  core_ret_t create_anonymous_shm() noexcept;
  core_ret_t attach_from_fd(int32_t fd) noexcept;

  void * get_slot_pointer(uint32_t slot_index) const noexcept override;
  uint32_t get_num_slots() const noexcept override;
  size64_t get_slot_size() const noexcept override;
  int32_t get_shm_fd() const noexcept;
  bool8_t is_valid() const noexcept override;

private:
  void cleanup() noexcept;

  uint32_t m_num_slots;
  size64_t m_slot_size;
  size64_t m_total_size;
  int32_t m_shm_fd;
  void * m_mapped_ptr;
  bool8_t m_is_owner;
};

} // namespace ipc
} // namespace base_node
