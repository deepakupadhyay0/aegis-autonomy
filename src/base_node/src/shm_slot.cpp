#include "base_node/ipc/shm_slot.hpp"

#include <limits>

namespace base_node
{
namespace ipc
{

namespace
{

constexpr uint64_t SLOT_STATE_MASK = 0x3U;
constexpr uint64_t MAX_SLOT_SEQUENCE = std::numeric_limits<uint64_t>::max() >> 2U;

enum class slot_state_e : uint8_t
{
  free = 0U,
  writing = 1U,
  ready = 2U,
  reading = 3U
};

uint64_t encode_slot_control(
  const uint64_t sequence,
  const slot_state_e state) noexcept
{
  return (sequence << 2U) | static_cast<uint64_t>(state);
}

slot_state_e decode_slot_state(const uint64_t slot_control) noexcept
{
  return static_cast<slot_state_e>(slot_control & SLOT_STATE_MASK);
}

bool8_t transition_slot(
  uint64_t & slot_control,
  const uint64_t sequence,
  const slot_state_e expected_state,
  const slot_state_e desired_state) noexcept
{
  uint64_t expected = encode_slot_control(sequence, expected_state);
  const uint64_t desired = encode_slot_control(sequence, desired_state);
  return __atomic_compare_exchange_n(
    &slot_control, &expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static_assert(
  __atomic_always_lock_free(sizeof(uint64_t), nullptr),
  "Shared-memory slot control requires lock-free 64-bit atomics.");

}  // namespace

bool8_t is_valid_slot_sequence(const uint64_t sequence) noexcept
{
  return sequence > 0U && sequence <= MAX_SLOT_SEQUENCE;
}

bool8_t try_begin_slot_write(
  uint64_t & slot_control,
  const uint64_t sequence) noexcept
{
  if (!is_valid_slot_sequence(sequence)) {
    return false;
  }

  uint64_t expected = __atomic_load_n(&slot_control, __ATOMIC_SEQ_CST);
  if (decode_slot_state(expected) != slot_state_e::free) {
    return false;
  }

  const uint64_t desired = encode_slot_control(sequence, slot_state_e::writing);
  return __atomic_compare_exchange_n(
    &slot_control, &expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

bool8_t publish_slot(
  uint64_t & slot_control,
  const uint64_t sequence) noexcept
{
  return is_valid_slot_sequence(sequence) &&
         transition_slot(
    slot_control, sequence, slot_state_e::writing, slot_state_e::ready);
}

bool8_t try_begin_slot_read(
  uint64_t & slot_control,
  const uint64_t sequence) noexcept
{
  return is_valid_slot_sequence(sequence) &&
         transition_slot(
    slot_control, sequence, slot_state_e::ready, slot_state_e::reading);
}

bool8_t release_slot(
  uint64_t & slot_control,
  const uint64_t sequence) noexcept
{
  return is_valid_slot_sequence(sequence) &&
         transition_slot(
    slot_control, sequence, slot_state_e::reading, slot_state_e::free);
}

}  // namespace ipc
}  // namespace base_node
