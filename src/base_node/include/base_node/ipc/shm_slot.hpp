#pragma once

#include "base_node/core_defs.hpp"
#include "base_node/visibility_control.hpp"

#include <cstddef>
#include <cstdint>

namespace base_node
{
namespace ipc
{

constexpr size_t SHM_SLOT_CONTROL_SIZE = 64U;

BASE_NODE_PUBLIC bool8_t is_valid_slot_sequence(uint64_t sequence) noexcept;

BASE_NODE_PUBLIC bool8_t try_begin_slot_write(
  uint64_t & slot_control,
  uint64_t sequence) noexcept;

BASE_NODE_PUBLIC bool8_t publish_slot(
  uint64_t & slot_control,
  uint64_t sequence) noexcept;

BASE_NODE_PUBLIC bool8_t try_begin_slot_read(
  uint64_t & slot_control,
  uint64_t sequence) noexcept;

BASE_NODE_PUBLIC bool8_t release_slot(
  uint64_t & slot_control,
  uint64_t sequence) noexcept;

}  // namespace ipc
}  // namespace base_node
