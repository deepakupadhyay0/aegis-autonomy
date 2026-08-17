#include "logging/node_logging.hpp"

#include "logging/logger.hpp"

#include <stdexcept>

namespace logging
{

node_logging_adapter_c::node_logging_adapter_c() noexcept
: m_initialized(false)
{
}

node_logging_adapter_c::~node_logging_adapter_c() noexcept
{
  this->shutdown();
}

void node_logging_adapter_c::initialize(const std::string_view node_name)
{
  bool expected = false;
  if (!m_initialized.compare_exchange_strong(
      expected,
      true,
      std::memory_order_acq_rel,
      std::memory_order_acquire))
  {
    throw std::logic_error("Node logging service is already initialized");
  }

  try {
    logger_c::initialize(node_name);
  } catch (...) {
    m_initialized.store(false, std::memory_order_release);
    throw;
  }
}

void node_logging_adapter_c::shutdown() noexcept
{
  if (!m_initialized.exchange(false, std::memory_order_acq_rel)) {
    return;
  }
  logger_c::shutdown();
}

bool node_logging_adapter_c::is_initialized() const noexcept
{
  return
    m_initialized.load(std::memory_order_acquire) &&
    logger_c::is_initialized();
}

void node_logging_adapter_c::write(
  const base_core::observability::log_level_e level,
  const std::string_view message,
  const std::source_location & location) noexcept
{
  if (!this->is_initialized()) {
    return;
  }
  logger_c::get_logger().log(level, message, location);
}

}  // namespace logging
