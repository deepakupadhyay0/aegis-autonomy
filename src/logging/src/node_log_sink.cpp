#include "logging/node_log_sink.hpp"

#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <array>
#include <cinttypes>
#include <cstdio>
#include <ctime>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace logging
{

namespace
{

constexpr std::size_t MAX_LOG_FILE_SIZE_BYTES = 10U * 1024U * 1024U;
constexpr std::size_t LOG_ARCHIVE_COUNT = 2U;
constexpr std::size_t FORMATTED_LOG_CAPACITY = 512U;

std::string sanitize_node_name(const std::string_view node_name)
{
  std::string result;
  result.reserve(node_name.size());
  for (const char character : node_name) {
    if (character == '/' && result.empty()) {
      continue;
    }
    const bool is_alphanumeric =
      (character >= 'a' && character <= 'z') ||
      (character >= 'A' && character <= 'Z') ||
      (character >= '0' && character <= '9');
    result.push_back(
      is_alphanumeric || character == '_' || character == '-' ?
      character :
      '_');
  }
  return result.empty() ? std::string("node") : result;
}

std::string_view level_name(const common::logging::log_level_e level) noexcept
{
  switch (level) {
    case common::logging::log_level_e::debug:
      return "DEBUG";
    case common::logging::log_level_e::info:
      return "INFO";
    case common::logging::log_level_e::warning:
      return "WARN";
    case common::logging::log_level_e::error:
      return "ERROR";
    case common::logging::log_level_e::fatal:
      return "FATAL";
    default:
      return "UNKNOWN";
  }
}

spdlog::level::level_enum to_spdlog_level(
  const common::logging::log_level_e level) noexcept
{
  switch (level) {
    case common::logging::log_level_e::debug:
      return spdlog::level::debug;
    case common::logging::log_level_e::info:
      return spdlog::level::info;
    case common::logging::log_level_e::warning:
      return spdlog::level::warn;
    case common::logging::log_level_e::error:
      return spdlog::level::err;
    case common::logging::log_level_e::fatal:
      return spdlog::level::critical;
    default:
      return spdlog::level::off;
  }
}

std::string_view format_record(
  const common::logging::log_record_s & record,
  std::array<char, FORMATTED_LOG_CAPACITY> & output) noexcept
{
  const std::time_t seconds = static_cast<std::time_t>(
    record.timestamp_ns / 1000000000U);
  const uint32_t nanoseconds = static_cast<uint32_t>(
    record.timestamp_ns % 1000000000U);
  std::tm utc_time{};
  if (::gmtime_r(&seconds, &utc_time) == nullptr) {
    utc_time = std::tm{};
  }

  const std::string_view thread_name = record.thread_name.view();
  const std::string_view source_file = record.source_file.view();
  const std::string_view message = record.message.view();
  const std::string_view mode = level_name(record.level);
  const int32_t formatted_size = static_cast<int32_t>(std::snprintf(
      output.data(),
      output.size(),
      "%04" PRId32 "-%02" PRId32 "-%02" PRId32
      "T%02" PRId32 ":%02" PRId32 ":%02" PRId32
      ".%09" PRIu32 "Z: %.*s: %.*s: %.*s:%" PRIu32 ": %.*s",
      static_cast<int32_t>(utc_time.tm_year + 1900),
      static_cast<int32_t>(utc_time.tm_mon + 1),
      static_cast<int32_t>(utc_time.tm_mday),
      static_cast<int32_t>(utc_time.tm_hour),
      static_cast<int32_t>(utc_time.tm_min),
      static_cast<int32_t>(utc_time.tm_sec),
      nanoseconds,
      static_cast<int32_t>(thread_name.size()),
      thread_name.data(),
      static_cast<int32_t>(mode.size()),
      mode.data(),
      static_cast<int32_t>(source_file.size()),
      source_file.data(),
      record.source_line,
      static_cast<int32_t>(message.size()),
      message.data()));
  if (formatted_size <= 0) {
    return std::string_view();
  }
  const std::size_t bounded_size =
    static_cast<std::size_t>(formatted_size) < output.size() ?
    static_cast<std::size_t>(formatted_size) :
    output.size() - 1U;
  return std::string_view(output.data(), bounded_size);
}

}  // namespace

node_log_sink_c::node_log_sink_c(
  const std::filesystem::path & log_directory,
  const common::logging::log_registration_s & registration,
  const std::shared_ptr<spdlog::details::thread_pool> & thread_pool)
: m_logger()
{
  const std::string logger_name = sanitize_node_name(
    registration.node_name.view());
  const std::filesystem::path log_file =
    log_directory / (logger_name + ".log");

  std::vector<spdlog::sink_ptr> sinks;
  sinks.reserve(registration.enable_console_log ? 2U : 1U);
  sinks.emplace_back(
    std::make_shared<spdlog::sinks::rotating_file_sink_st>(
      log_file.string(),
      MAX_LOG_FILE_SIZE_BYTES,
      LOG_ARCHIVE_COUNT,
      false));
  if (registration.enable_console_log) {
    sinks.emplace_back(
      std::make_shared<spdlog::sinks::stdout_color_sink_st>());
  }

  m_logger = std::make_shared<spdlog::async_logger>(
    logger_name,
    sinks.begin(),
    sinks.end(),
    thread_pool,
    spdlog::async_overflow_policy::overrun_oldest);
  m_logger->set_level(spdlog::level::debug);
  m_logger->set_pattern("%^%v%$");
}

node_log_sink_c::~node_log_sink_c() noexcept
{
  if (m_logger != nullptr) {
    try {
      m_logger->flush();
    } catch (...) {
    }
  }
}

void node_log_sink_c::write(
  const common::logging::log_record_s & record) noexcept
{
  if (m_logger == nullptr) {
    return;
  }
  std::array<char, FORMATTED_LOG_CAPACITY> output{};
  const std::string_view formatted_record = format_record(record, output);
  if (formatted_record.empty()) {
    return;
  }
  try {
    m_logger->log(
      to_spdlog_level(record.level),
      spdlog::string_view_t(
        formatted_record.data(),
        formatted_record.size()));
  } catch (...) {
  }
}

}  // namespace logging
