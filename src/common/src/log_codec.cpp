#include "common/logging/log_codec.hpp"

#include <cstring>
#include <string_view>

namespace common
{
namespace logging
{

namespace
{

constexpr std::size_t REGISTRATION_TYPE_OFFSET = 0U;
constexpr std::size_t REGISTRATION_NODE_SIZE_OFFSET = 1U;
constexpr std::size_t REGISTRATION_NODE_OFFSET = 2U;
constexpr std::size_t REGISTRATION_CONSOLE_OFFSET = 66U;

constexpr std::size_t RECORD_TYPE_OFFSET = 0U;
constexpr std::size_t RECORD_TIMESTAMP_OFFSET = 1U;
constexpr std::size_t RECORD_DROPPED_COUNT_OFFSET = 9U;
constexpr std::size_t RECORD_SOURCE_LINE_OFFSET = 17U;
constexpr std::size_t RECORD_LEVEL_OFFSET = 21U;
constexpr std::size_t RECORD_THREAD_SIZE_OFFSET = 22U;
constexpr std::size_t RECORD_THREAD_OFFSET = 23U;
constexpr std::size_t RECORD_SOURCE_FILE_SIZE_OFFSET = 39U;
constexpr std::size_t RECORD_SOURCE_FILE_OFFSET = 40U;
constexpr std::size_t RECORD_MESSAGE_SIZE_OFFSET = 104U;
constexpr std::size_t RECORD_MESSAGE_OFFSET = 106U;

static_assert(
  REGISTRATION_CONSOLE_OFFSET + sizeof(uint8_t) ==
  LOG_REGISTRATION_WIRE_SIZE);
static_assert(
  RECORD_MESSAGE_OFFSET + string256_t::capacity() ==
  LOG_RECORD_WIRE_SIZE);

template<typename value_t>
void write_value(std::byte * const destination, const value_t value) noexcept
{
  std::memcpy(destination, &value, sizeof(value));
}

template<typename value_t>
value_t read_value(const std::byte * const source) noexcept
{
  value_t value{};
  std::memcpy(&value, source, sizeof(value));
  return value;
}

void write_text(
  std::byte * const destination,
  const std::string_view text,
  const std::size_t capacity) noexcept
{
  std::memset(destination, 0, capacity);
  std::memcpy(destination, text.data(), text.size());
}

bool is_valid_level(const log_level_e level) noexcept
{
  return level >= log_level_e::debug && level <= log_level_e::fatal;
}

bool contains_null(
  const std::byte * const text,
  const std::size_t text_size) noexcept
{
  return text_size > 0U &&
    std::memchr(text, '\0', text_size) != nullptr;
}

}  // namespace

log_packet_type_e get_log_packet_type(
  const std::byte * const buffer,
  const std::size_t buffer_size) noexcept
{
  if (buffer == nullptr || buffer_size == 0U) {
    return static_cast<log_packet_type_e>(0U);
  }
  return static_cast<log_packet_type_e>(
    read_value<uint8_t>(&buffer[0U]));
}

void serialize_log_registration(
  const log_registration_s & registration,
  std::array<std::byte, LOG_REGISTRATION_WIRE_SIZE> & buffer) noexcept
{
  buffer.fill(std::byte{0});
  write_value<uint8_t>(
    &buffer[REGISTRATION_TYPE_OFFSET],
    static_cast<uint8_t>(log_packet_type_e::registration));
  write_value<uint8_t>(
    &buffer[REGISTRATION_NODE_SIZE_OFFSET],
    static_cast<uint8_t>(registration.node_name.size()));
  write_text(
    &buffer[REGISTRATION_NODE_OFFSET],
    registration.node_name.view(),
    string64_t::capacity());
  write_value<uint8_t>(
    &buffer[REGISTRATION_CONSOLE_OFFSET],
    registration.enable_console_log ? 1U : 0U);
}

bool deserialize_log_registration(
  const std::byte * const buffer,
  const std::size_t buffer_size,
  log_registration_s & registration) noexcept
{
  if (buffer == nullptr || buffer_size != LOG_REGISTRATION_WIRE_SIZE ||
    get_log_packet_type(buffer, buffer_size) !=
    log_packet_type_e::registration)
  {
    return false;
  }

  const uint8_t node_name_size =
    read_value<uint8_t>(&buffer[REGISTRATION_NODE_SIZE_OFFSET]);
  const uint8_t console_enabled =
    read_value<uint8_t>(&buffer[REGISTRATION_CONSOLE_OFFSET]);
  if (node_name_size == 0U || node_name_size > string64_t::capacity() ||
    console_enabled > 1U ||
    contains_null(&buffer[REGISTRATION_NODE_OFFSET], node_name_size))
  {
    return false;
  }

  registration.node_name.assign(
    std::string_view(
      reinterpret_cast<const char *>(&buffer[REGISTRATION_NODE_OFFSET]),
      node_name_size));
  registration.enable_console_log = console_enabled != 0U;
  return true;
}

void serialize_log_record(
  const log_record_s & record,
  std::array<std::byte, LOG_RECORD_WIRE_SIZE> & buffer) noexcept
{
  buffer.fill(std::byte{0});
  write_value<uint8_t>(
    &buffer[RECORD_TYPE_OFFSET],
    static_cast<uint8_t>(log_packet_type_e::record));
  write_value<uint64_t>(&buffer[RECORD_TIMESTAMP_OFFSET], record.timestamp_ns);
  write_value<uint64_t>(
    &buffer[RECORD_DROPPED_COUNT_OFFSET],
    record.dropped_record_count);
  write_value<uint32_t>(&buffer[RECORD_SOURCE_LINE_OFFSET], record.source_line);
  write_value<uint8_t>(
    &buffer[RECORD_LEVEL_OFFSET],
    static_cast<uint8_t>(record.level));
  write_value<uint8_t>(
    &buffer[RECORD_THREAD_SIZE_OFFSET],
    static_cast<uint8_t>(record.thread_name.size()));
  write_text(
    &buffer[RECORD_THREAD_OFFSET],
    record.thread_name.view(),
    string16_t::capacity());
  write_value<uint8_t>(
    &buffer[RECORD_SOURCE_FILE_SIZE_OFFSET],
    static_cast<uint8_t>(record.source_file.size()));
  write_text(
    &buffer[RECORD_SOURCE_FILE_OFFSET],
    record.source_file.view(),
    string64_t::capacity());
  write_value<uint16_t>(
    &buffer[RECORD_MESSAGE_SIZE_OFFSET],
    static_cast<uint16_t>(record.message.size()));
  write_text(
    &buffer[RECORD_MESSAGE_OFFSET],
    record.message.view(),
    string256_t::capacity());
}

bool deserialize_log_record(
  const std::byte * const buffer,
  const std::size_t buffer_size,
  log_record_s & record) noexcept
{
  if (buffer == nullptr || buffer_size != LOG_RECORD_WIRE_SIZE ||
    get_log_packet_type(buffer, buffer_size) != log_packet_type_e::record)
  {
    return false;
  }

  const log_level_e level = static_cast<log_level_e>(
    read_value<uint8_t>(&buffer[RECORD_LEVEL_OFFSET]));
  const uint8_t thread_name_size =
    read_value<uint8_t>(&buffer[RECORD_THREAD_SIZE_OFFSET]);
  const uint8_t source_file_size =
    read_value<uint8_t>(&buffer[RECORD_SOURCE_FILE_SIZE_OFFSET]);
  const uint16_t message_size =
    read_value<uint16_t>(&buffer[RECORD_MESSAGE_SIZE_OFFSET]);
  if (!is_valid_level(level) ||
    thread_name_size > string16_t::capacity() ||
    source_file_size > string64_t::capacity() ||
    message_size > string256_t::capacity() ||
    contains_null(&buffer[RECORD_THREAD_OFFSET], thread_name_size) ||
    contains_null(&buffer[RECORD_SOURCE_FILE_OFFSET], source_file_size) ||
    contains_null(&buffer[RECORD_MESSAGE_OFFSET], message_size))
  {
    return false;
  }

  record.timestamp_ns =
    read_value<uint64_t>(&buffer[RECORD_TIMESTAMP_OFFSET]);
  record.dropped_record_count =
    read_value<uint64_t>(&buffer[RECORD_DROPPED_COUNT_OFFSET]);
  record.source_line =
    read_value<uint32_t>(&buffer[RECORD_SOURCE_LINE_OFFSET]);
  record.level = level;
  record.thread_name.assign(
    std::string_view(
      reinterpret_cast<const char *>(&buffer[RECORD_THREAD_OFFSET]),
      thread_name_size));
  record.source_file.assign(
    std::string_view(
      reinterpret_cast<const char *>(&buffer[RECORD_SOURCE_FILE_OFFSET]),
      source_file_size));
  record.message.assign(
    std::string_view(
      reinterpret_cast<const char *>(&buffer[RECORD_MESSAGE_OFFSET]),
      message_size));
  return true;
}

}  // namespace logging
}  // namespace common
