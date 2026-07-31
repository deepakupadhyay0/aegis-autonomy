#include "logging/logger.hpp"

#include "common/version.h"
#include "logging/log_backend.hpp"
#include "logging/logging_config.hpp"

#include <array>
#include <cinttypes>
#include <cstdio>
#include <exception>
#include <source_location>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace logging
{

std::atomic<logger_c *> logger_c::m_instance{nullptr};
std::atomic<logger_c::initialization_state_e>
logger_c::m_initialization_state{initialization_state_e::uninitialized};
std::unique_ptr<logger_c> logger_c::m_instance_owner;

logger_c::logger_c(
  const construction_token_s,
  const std::string_view node_name,
  const logging_options_s & options)
: m_backend(std::make_unique<local_log_backend_c>(node_name, options))
{
  this->log_build_information();
}

logger_c::~logger_c() noexcept = default;

logger_c & logger_c::get_logger() noexcept
{
  logger_c * const instance = m_instance.load(std::memory_order_acquire);
  if (instance == nullptr) {
    std::terminate();
  }
  return *instance;
}

bool logger_c::is_initialized() noexcept
{
  return m_instance.load(std::memory_order_acquire) != nullptr;
}

void logger_c::initialize(
  const std::string_view node_name)
{
  initialization_state_e expected_state = initialization_state_e::uninitialized;
  if (!m_initialization_state.compare_exchange_strong(
      expected_state,
      initialization_state_e::initializing,
      std::memory_order_acq_rel,
      std::memory_order_acquire))
  {
    throw std::logic_error("Logger is already initialized in this process");
  }

  try {
    std::unique_ptr<logger_c> instance = std::make_unique<logger_c>(
      construction_token_s{},
      node_name,
      load_logging_options());
    logger_c * const instance_pointer = instance.get();
    m_instance_owner = std::move(instance);
    // Ownership is complete before release-publishing the hot-path pointer.
    m_instance.store(instance_pointer, std::memory_order_release);
    m_initialization_state.store(
      initialization_state_e::initialized,
      std::memory_order_release);
  } catch (...) {
    m_initialization_state.store(
      initialization_state_e::uninitialized,
      std::memory_order_release);
    throw;
  }
}

void logger_c::shutdown() noexcept
{
  initialization_state_e expected_state = initialization_state_e::initialized;
  if (!m_initialization_state.compare_exchange_strong(
      expected_state,
      initialization_state_e::shutting_down,
      std::memory_order_acq_rel,
      std::memory_order_acquire))
  {
    return;
  }

  // Prevent new hot-path lookups before destroying the owned instance.
  logger_c * const instance = m_instance.exchange(
    nullptr,
    std::memory_order_acq_rel);
  if (instance != nullptr && instance->m_backend != nullptr) {
    instance->m_backend->shutdown();
  }
  m_instance_owner.reset();
  m_initialization_state.store(
    initialization_state_e::uninitialized,
    std::memory_order_release);
}

bool logger_c::should_log(const log_level_e level) const noexcept
{
  return m_backend != nullptr && m_backend->should_log(level);
}

void logger_c::log(
  const log_level_e level,
  const std::string_view message,
  const std::source_location & location) noexcept
{
  if (m_backend != nullptr && m_backend->should_log(level)) {
    m_backend->enqueue(level, message, location);
  }
}

void logger_c::log_build_information() noexcept
{
  if (m_backend == nullptr) {
    return;
  }

  std::array<char, common::string256_t::capacity() + 1U> message{};
  const std::source_location location = std::source_location::current();
  const auto enqueue_field =
    [this, &message, &location](
      const std::string_view name,
      const std::string_view value) noexcept
    {
      const common::int32_t formatted_size =
        static_cast<common::int32_t>(std::snprintf(
            message.data(),
            message.size(),
            "%.*s: %.*s",
            static_cast<common::int32_t>(name.size()),
            name.data(),
            static_cast<common::int32_t>(value.size()),
            value.data()));
      if (formatted_size <= 0) {
        return;
      }
      const std::size_t message_size =
        static_cast<std::size_t>(formatted_size) < message.size() ?
        static_cast<std::size_t>(formatted_size) :
        message.size() - 1U;
      m_backend->enqueue(
        log_level_e::info,
        std::string_view(message.data(), message_size),
        location);
    };

  enqueue_field("CMAKE_PROJECT", CORE_LOGGER_CMAKE_PROJECT);
  enqueue_field("PACKAGE_VERSION", CORE_LOGGER_PACKAGE_VERSION);
  enqueue_field("GIT_PROJECT", CORE_LOGGER_GIT_PROJECT);
  enqueue_field("LAST_SHA", CORE_LOGGER_LAST_SHA);
  enqueue_field("CICD_SHA", CORE_LOGGER_CICD_SHA);
  enqueue_field("BRANCH_NAME", CORE_LOGGER_BRANCH_NAME);
  enqueue_field("BUILD_TIMESTAMP", CORE_LOGGER_BUILD_TIMESTAMP);

  std::array<char, 48U> version_string{};
  const common::int32_t version_size =
    static_cast<common::int32_t>(std::snprintf(
        version_string.data(),
        version_string.size(),
        "%" PRIu32 ".%" PRIu32 ".%" PRIu32 ".%" PRIu32,
        common::version::MAJOR,
        common::version::MINOR,
        common::version::PATCH,
        common::version::BUILD));
  if (version_size > 0) {
    const std::size_t bounded_version_size =
      static_cast<std::size_t>(version_size) < version_string.size() ?
      static_cast<std::size_t>(version_size) :
      version_string.size() - 1U;
    enqueue_field(
      "VERSION_STR",
      std::string_view(version_string.data(), bounded_version_size));
  }
}

void logger_c::debug(
  const std::string_view message,
  const std::source_location & location) noexcept
{
  this->log(log_level_e::debug, message, location);
}

void logger_c::info(
  const std::string_view message,
  const std::source_location & location) noexcept
{
  this->log(log_level_e::info, message, location);
}

void logger_c::warning(
  const std::string_view message,
  const std::source_location & location) noexcept
{
  this->log(log_level_e::warning, message, location);
}

void logger_c::error(
  const std::string_view message,
  const std::source_location & location) noexcept
{
  this->log(log_level_e::error, message, location);
}

void logger_c::fatal(
  const std::string_view message,
  const std::source_location & location) noexcept
{
  this->log(log_level_e::fatal, message, location);
}

}  // namespace logging
