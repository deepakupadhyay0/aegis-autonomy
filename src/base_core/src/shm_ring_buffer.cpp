#include "base_core/ipc/shm_ring_buffer.hpp"
#include "base_core/ipc/shm_slot.hpp"

#include <cstring>
#include <limits>

namespace base_core
{
namespace ipc
{

namespace
{

struct shm_ring_header_s
{
  uint64_t slot_size;
  uint32_t num_slots;
  uint32_t metadata_size;
};

static_assert(sizeof(shm_ring_header_s) == 16U);

bool8_t calculate_layout(
  const uint32_t num_slots,
  const size64_t slot_size,
  const uint32_t metadata_size,
  size64_t & control_offset,
  size64_t & data_offset,
  size64_t & total_size) noexcept
{
  constexpr size64_t alignment = static_cast<size64_t>(SHM_SLOT_CONTROL_SIZE);
  constexpr size64_t alignment_mask = alignment - 1U;
  constexpr size64_t header_size = sizeof(shm_ring_header_s);
  constexpr size64_t max_size = std::numeric_limits<size64_t>::max();

  if (num_slots == 0U || slot_size == 0U ||
    header_size + metadata_size > max_size - alignment_mask)
  {
    return false;
  }

  control_offset = (header_size + metadata_size + alignment_mask) & ~alignment_mask;
  if (num_slots > (max_size - control_offset) / SHM_SLOT_CONTROL_SIZE) {
    return false;
  }

  data_offset =
    control_offset + (static_cast<size64_t>(num_slots) * SHM_SLOT_CONTROL_SIZE);
  if (slot_size > (max_size - data_offset) / num_slots) {
    return false;
  }

  total_size = data_offset + (slot_size * num_slots);
  return true;
}

}  // namespace

shm_ring_buffer_c::shm_ring_buffer_c() noexcept
: shm_ring_buffer_c(0U, 0U)
{
}

shm_ring_buffer_c::shm_ring_buffer_c(
  const uint32_t num_slots,
  const size64_t slot_size) noexcept
: m_shared_memory(),
  m_num_slots(num_slots),
  m_slot_size(slot_size),
  m_metadata_size(0U),
  m_control_offset(0U),
  m_data_offset(0U)
{
}

shm_ring_buffer_c::~shm_ring_buffer_c() noexcept = default;

core_ret_t shm_ring_buffer_c::create_anonymous_shm(
  const std::string_view name,
  const std::span<const std::byte> metadata) noexcept
{
  m_shared_memory.reset();
  m_control_offset = 0U;
  m_data_offset = 0U;

  if (metadata.size() > std::numeric_limits<uint32_t>::max()) {
    return core_ret_e::too_large;
  }
  m_metadata_size = static_cast<uint32_t>(metadata.size());

  size64_t total_size = 0U;
  if (!calculate_layout(
      m_num_slots,
      m_slot_size,
      m_metadata_size,
      m_control_offset,
      m_data_offset,
      total_size))
  {
    return core_ret_e::bad_arg;
  }

  const core_ret_t create_result =
    m_shared_memory.create_anonymous(name, total_size);
  if (create_result != core_ret_e::ok) {
    m_control_offset = 0U;
    m_data_offset = 0U;
    return create_result;
  }

  const std::span<std::byte> memory = m_shared_memory.memory();
  std::memset(memory.data(), 0, memory.size());

  const shm_ring_header_s header{
    m_slot_size,
    m_num_slots,
    m_metadata_size};
  std::memcpy(memory.data(), &header, sizeof(header));
  if (!metadata.empty()) {
    std::memcpy(
      memory.data() + sizeof(shm_ring_header_s),
      metadata.data(),
      metadata.size());
  }

  return core_ret_e::ok;
}

core_ret_t shm_ring_buffer_c::attach_from_fd(const int32_t fd) noexcept
{
  m_shared_memory.reset();
  m_num_slots = 0U;
  m_slot_size = 0U;
  m_metadata_size = 0U;
  m_control_offset = 0U;
  m_data_offset = 0U;

  const core_ret_t attach_result = m_shared_memory.attach_from_fd(fd);
  if (attach_result != core_ret_e::ok) {
    return attach_result;
  }

  const std::span<const std::byte> memory = m_shared_memory.memory();
  if (memory.size() < sizeof(shm_ring_header_s)) {
    m_shared_memory.reset();
    return core_ret_e::bad_arg;
  }

  shm_ring_header_s header{};
  std::memcpy(&header, memory.data(), sizeof(header));

  size64_t total_size = 0U;
  if (!calculate_layout(
      header.num_slots,
      header.slot_size,
      header.metadata_size,
      m_control_offset,
      m_data_offset,
      total_size) ||
    total_size != m_shared_memory.size())
  {
    m_shared_memory.reset();
    m_control_offset = 0U;
    m_data_offset = 0U;
    return core_ret_e::bad_arg;
  }

  m_num_slots = header.num_slots;
  m_slot_size = header.slot_size;
  m_metadata_size = header.metadata_size;
  return core_ret_e::ok;
}

void * shm_ring_buffer_c::get_slot_pointer(const uint32_t slot_index) const noexcept
{
  if (slot_index >= m_num_slots || !this->is_valid()) {
    return nullptr;
  }

  const std::span<const std::byte> memory = m_shared_memory.memory();
  return const_cast<std::byte *>(
    memory.data() + m_data_offset +
    (static_cast<size64_t>(slot_index) * m_slot_size));
}

std::span<const std::byte> shm_ring_buffer_c::get_metadata() const noexcept
{
  if (!this->is_valid()) {
    return {};
  }

  const std::span<const std::byte> memory = m_shared_memory.memory();
  return {
    memory.data() + sizeof(shm_ring_header_s),
    static_cast<size_t>(m_metadata_size)};
}

uint64_t * shm_ring_buffer_c::get_slot_control_pointer(
  const uint32_t slot_index) const noexcept
{
  if (slot_index >= m_num_slots || !this->is_valid()) {
    return nullptr;
  }

  const std::span<const std::byte> memory = m_shared_memory.memory();
  return reinterpret_cast<uint64_t *>(
    const_cast<std::byte *>(
      memory.data() + m_control_offset +
      (static_cast<size64_t>(slot_index) * SHM_SLOT_CONTROL_SIZE)));
}

bool8_t shm_ring_buffer_c::try_acquire_slot_for_write(
  const uint64_t sequence,
  uint32_t & slot_index) noexcept
{
  if (!this->is_valid() || !is_valid_slot_sequence(sequence)) {
    return false;
  }

  const uint32_t first_slot = slot_index % m_num_slots;
  for (uint32_t offset = 0U; offset < m_num_slots; ++offset) {
    const uint32_t candidate = (first_slot + offset) % m_num_slots;
    uint64_t * const control_ptr = this->get_slot_control_pointer(candidate);
    if (control_ptr != nullptr && try_begin_slot_write(*control_ptr, sequence)) {
      slot_index = candidate;
      return true;
    }
  }
  return false;
}

core_ret_t shm_ring_buffer_c::publish_written_slot(
  const uint32_t slot_index,
  const uint64_t sequence) noexcept
{
  uint64_t * const control_ptr = this->get_slot_control_pointer(slot_index);
  if (control_ptr == nullptr || !is_valid_slot_sequence(sequence)) {
    return core_ret_e::bad_arg;
  }

  return publish_slot(*control_ptr, sequence) ?
         core_ret_e::ok : core_ret_e::wrong_object;
}

bool8_t shm_ring_buffer_c::try_acquire_slot_for_read(
  const uint32_t slot_index,
  const uint64_t sequence) noexcept
{
  uint64_t * const control_ptr = this->get_slot_control_pointer(slot_index);
  if (control_ptr == nullptr || !is_valid_slot_sequence(sequence)) {
    return false;
  }

  return try_begin_slot_read(*control_ptr, sequence);
}

core_ret_t shm_ring_buffer_c::release_read_slot(
  const uint32_t slot_index,
  const uint64_t sequence) noexcept
{
  uint64_t * const control_ptr = this->get_slot_control_pointer(slot_index);
  if (control_ptr == nullptr || !is_valid_slot_sequence(sequence)) {
    return core_ret_e::bad_arg;
  }

  return release_slot(*control_ptr, sequence) ?
         core_ret_e::ok : core_ret_e::wrong_object;
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
  return m_shared_memory.handle();
}

bool8_t shm_ring_buffer_c::is_valid() const noexcept
{
  return m_shared_memory.is_valid() &&
         m_num_slots > 0U &&
         m_slot_size > 0U &&
         m_control_offset >= sizeof(shm_ring_header_s) + m_metadata_size &&
         m_data_offset > m_control_offset &&
         m_data_offset < m_shared_memory.size();
}

}  // namespace ipc
}  // namespace base_core
