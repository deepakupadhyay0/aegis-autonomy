#pragma once

#include "base_node/core_defs.hpp"
#include <cstdint>
#include <cstddef>

namespace base_node
{
namespace ipc
{

class raw_buffer_base_c
{
public:
  virtual ~raw_buffer_base_c() = default;

  virtual void * get_slot_pointer(uint32_t slot_index) const noexcept = 0;
  virtual uint32_t get_num_slots() const noexcept = 0;
  virtual size64_t get_slot_size() const noexcept = 0;
  virtual bool8_t is_valid() const noexcept = 0;
};

}  // namespace ipc
}  // namespace base_node
