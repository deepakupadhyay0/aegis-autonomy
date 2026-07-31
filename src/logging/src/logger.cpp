#include "logging/logger.hpp"

#include "logging/log_client.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

namespace logging
{

std::unique_ptr<logger_c> logger_c::m_instance;

logger_c::logger_c(
  const construction_token_s,
  const std::string_view node_name,
  const logging_options_s & options)
: m_backend(std::make_unique<log_client_c>(node_name, options))
{
}

logger_c::~logger_c() noexcept = default;

logger_c & logger_c::get_logger() noexcept
{
  if (m_instance == nullptr) {
    std::terminate();
  }
  return *m_instance;
}

bool logger_c::is_initialized() noexcept
{
  return m_instance != nullptr;
}

void logger_c::initialize(
  const std::string_view node_name,
  const logging_options_s & options)
{
  if (m_instance != nullptr) {
    throw std::logic_error("Logger is already initialized in this process");
  }
  m_instance = std::make_unique<logger_c>(
    construction_token_s{},
    node_name,
    options);
}

void logger_c::shutdown() noexcept
{
  if (m_instance != nullptr && m_instance->m_backend != nullptr) {
    m_instance->m_backend->shutdown();
  }
  m_instance.reset();
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
  if (m_backend != nullptr) {
    m_backend->enqueue(level, message, location);
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
