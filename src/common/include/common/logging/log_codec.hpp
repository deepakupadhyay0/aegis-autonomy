#pragma once

#include "common/logging/log_protocol.hpp"

#include <array>
#include <cstddef>

namespace common
{
namespace logging
{

inline constexpr std::size_t LOG_REGISTRATION_WIRE_SIZE = 67U;
inline constexpr std::size_t LOG_RECORD_WIRE_SIZE = 362U;
inline constexpr std::size_t MAX_LOG_PACKET_WIRE_SIZE = LOG_RECORD_WIRE_SIZE;

log_packet_type_e get_log_packet_type(
  const std::byte * buffer,
  std::size_t buffer_size) noexcept;

void serialize_log_registration(
  const log_registration_s & registration,
  std::array<std::byte, LOG_REGISTRATION_WIRE_SIZE> & buffer) noexcept;

bool deserialize_log_registration(
  const std::byte * buffer,
  std::size_t buffer_size,
  log_registration_s & registration) noexcept;

void serialize_log_record(
  const log_record_s & record,
  std::array<std::byte, LOG_RECORD_WIRE_SIZE> & buffer) noexcept;

bool deserialize_log_record(
  const std::byte * buffer,
  std::size_t buffer_size,
  log_record_s & record) noexcept;

}  // namespace logging
}  // namespace common
