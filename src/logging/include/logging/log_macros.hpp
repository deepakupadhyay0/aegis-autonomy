#pragma once

#include "logging/log_throttle.hpp"
#include "logging/logger.hpp"

#include <source_location>
#include <string_view>
#include <utility>

namespace logging
{
namespace detail
{

template<std::size_t message_size_v>
void log_macro_message(
  logger_c & logger,
  const log_level_e level,
  const std::source_location & location,
  const char (&message)[message_size_v]) noexcept
{
  static_assert(message_size_v > 0U, "Log message must include a terminator");
  static_assert(
    message_size_v - 1U <= common::string256_t::capacity(),
    "Log message exceeds fixed log message capacity");
  logger.log(
    level,
    std::string_view(message, message_size_v - 1U),
    location);
}

inline void log_macro_message(
  logger_c & logger,
  const log_level_e level,
  const std::source_location & location,
  const std::string_view message) noexcept
{
  logger.log(level, message, location);
}

template<
  std::size_t format_size_v,
  typename ... argument_ts>
requires(sizeof...(argument_ts) > 0U)
void log_macro_message(
  logger_c & logger,
  const log_level_e level,
  const std::source_location & location,
  const char (&format)[format_size_v],
  argument_ts && ... arguments) noexcept
{
  logger.log_format(
    level,
    location,
    format,
    std::forward<argument_ts>(arguments)...);
}

}  // namespace detail
}  // namespace logging

#define CORE_LOG_BASE(level, ...) \
  do { \
    ::logging::logger_c & core_log_logger = \
      ::logging::logger_c::get_logger(); \
    if (core_log_logger.should_log(level)) { \
      ::logging::detail::log_macro_message( \
        core_log_logger, \
        level, \
        std::source_location::current(), \
        __VA_ARGS__); \
    } \
  } while (false)

#define CORE_LOG_DEBUG(...) \
  CORE_LOG_BASE(::logging::log_level_e::debug, __VA_ARGS__)
#define CORE_LOG_INFO(...) \
  CORE_LOG_BASE(::logging::log_level_e::info, __VA_ARGS__)
#define CORE_LOG_WARN(...) \
  CORE_LOG_BASE(::logging::log_level_e::warning, __VA_ARGS__)
#define CORE_LOG_ERROR(...) \
  CORE_LOG_BASE(::logging::log_level_e::error, __VA_ARGS__)
#define CORE_LOG_FATAL(...) \
  CORE_LOG_BASE(::logging::log_level_e::fatal, __VA_ARGS__)

#define CORE_LOG_THROTTLE_BASE(level, interval, ...) \
  do { \
    ::logging::logger_c & core_log_logger = \
      ::logging::logger_c::get_logger(); \
    static ::logging::log_throttle_c core_log_throttle; \
    if (core_log_logger.should_log(level) && \
      core_log_throttle.ready(interval)) \
    { \
      ::logging::detail::log_macro_message( \
        core_log_logger, \
        level, \
        std::source_location::current(), \
        __VA_ARGS__); \
    } \
  } while (false)

#define CORE_LOG_DEBUG_THROTTLE(interval, ...) \
  CORE_LOG_THROTTLE_BASE( \
    ::logging::log_level_e::debug, interval, __VA_ARGS__)
#define CORE_LOG_INFO_THROTTLE(interval, ...) \
  CORE_LOG_THROTTLE_BASE( \
    ::logging::log_level_e::info, interval, __VA_ARGS__)
#define CORE_LOG_WARN_THROTTLE(interval, ...) \
  CORE_LOG_THROTTLE_BASE( \
    ::logging::log_level_e::warning, interval, __VA_ARGS__)
#define CORE_LOG_ERROR_THROTTLE(interval, ...) \
  CORE_LOG_THROTTLE_BASE( \
    ::logging::log_level_e::error, interval, __VA_ARGS__)
#define CORE_LOG_FATAL_THROTTLE(interval, ...) \
  CORE_LOG_THROTTLE_BASE( \
    ::logging::log_level_e::fatal, interval, __VA_ARGS__)
