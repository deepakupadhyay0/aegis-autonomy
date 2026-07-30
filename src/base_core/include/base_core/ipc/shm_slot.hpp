#pragma once

#include "base_core/core_defs.hpp"
#include "base_core/visibility_control.hpp"

#include <cstddef>
#include <cstdint>

namespace base_core
{
namespace ipc
{

constexpr size_t SHM_SLOT_CONTROL_SIZE = 64U;

BASE_CORE_PUBLIC bool8_t is_valid_slot_sequence(uint64_t sequence) noexcept;

BASE_CORE_PUBLIC bool8_t try_begin_slot_write(
  uint64_t & slot_control,
  uint64_t sequence) noexcept;

BASE_CORE_PUBLIC bool8_t publish_slot(
  uint64_t & slot_control,
  uint64_t sequence) noexcept;

BASE_CORE_PUBLIC bool8_t try_begin_slot_read(
  uint64_t & slot_control,
  uint64_t sequence) noexcept;

BASE_CORE_PUBLIC bool8_t release_slot(
  uint64_t & slot_control,
  uint64_t sequence) noexcept;

}  // namespace ipc
}  // namespace base_core
