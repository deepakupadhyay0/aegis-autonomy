#pragma once

#include "common/fixed_string.hpp"
#include "common/numeric_types.hpp"
#include "logging/log_types.hpp"
#include "logging/visibility_control.hpp"

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <source_location>
#include <string_view>
#include <utility>

namespace logging
{

class abstract_log_backend_c;
class node_logging_adapter_c;

/// Process-local logger facade initialized through node_logging_adapter_c.
/// get_logger() performs no synchronization on the logging hot path.
class LOGGING_PUBLIC logger_c final
{
  struct construction_token_s final
  {
  };

public:
  logger_c(
    construction_token_s,
    std::string_view node_name,
    const logging_options_s & options);
  ~logger_c() noexcept;

  logger_c(const logger_c &) = delete;
  logger_c & operator=(const logger_c &) = delete;
  logger_c(logger_c &&) = delete;
  logger_c & operator=(logger_c &&) = delete;

  static logger_c & get_logger() noexcept;
  static bool is_initialized() noexcept;

  bool should_log(log_level_e level) const noexcept;
  void log(
    log_level_e level,
    std::string_view message,
    const std::source_location & location =
    std::source_location::current()) noexcept;

  void debug(
    std::string_view message,
    const std::source_location & location =
    std::source_location::current()) noexcept;
  void info(
    std::string_view message,
    const std::source_location & location =
    std::source_location::current()) noexcept;
  void warning(
    std::string_view message,
    const std::source_location & location =
    std::source_location::current()) noexcept;
  void error(
    std::string_view message,
    const std::source_location & location =
    std::source_location::current()) noexcept;
  void fatal(
    std::string_view message,
    const std::source_location & location =
    std::source_location::current()) noexcept;

  template<std::size_t format_size_v, typename ... argument_ts>
  void log_format(
    const log_level_e level,
    const std::source_location & location,
    const char (& format)[format_size_v],
    argument_ts && ... arguments) noexcept
  {
    static_assert(format_size_v > 1U, "Log format cannot be empty");
    static_assert(
      format_size_v - 1U <= common::string256_t::capacity(),
      "Log format exceeds fixed log message capacity");
    if (!this->should_log(level)) {
      return;
    }

    std::array<char, common::string256_t::capacity() + 1U> message{};
    const int32_t formatted_size = static_cast<int32_t>(
      std::snprintf(
        message.data(),
        message.size(),
        format,
        std::forward<argument_ts>(arguments)...));
    if (formatted_size < 0) {
      this->log(level, "Log message formatting failed", location);
      return;
    }

    const std::size_t message_size =
      static_cast<std::size_t>(formatted_size) <
      common::string256_t::capacity() ?
      static_cast<std::size_t>(formatted_size) :
      common::string256_t::capacity();
    this->log(
      level,
      std::string_view(message.data(), message_size),
      location);
  }

private:
  enum class initialization_state_e : common::uint8_t
  {
    uninitialized = 0U,
    initializing,
    initialized,
    shutting_down
  };

  friend class node_logging_adapter_c;

  static void initialize(
    std::string_view node_name);
  static void shutdown() noexcept;
  void log_build_information() noexcept;

  /// Non-owning hot-path pointer, published only after m_instance_owner owns
  /// the fully constructed logger.
  static std::atomic<logger_c *> m_instance;
  static std::atomic<initialization_state_e> m_initialization_state;
  static std::unique_ptr<logger_c> m_instance_owner;
  std::unique_ptr<abstract_log_backend_c> m_backend;
};

}  // namespace logging
