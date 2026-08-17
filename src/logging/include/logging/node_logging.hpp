#pragma once

#include "base_core/node_logging.hpp"
#include "logging/visibility_control.hpp"

#include <atomic>
#include <source_location>
#include <string_view>

namespace logging
{

/// @brief Adapts logger_c to the optional base-node logging contract.
class LOGGING_PUBLIC node_logging_adapter_c final :
  public base_core::observability::node_logging_c
{
public:
  node_logging_adapter_c() noexcept;
  ~node_logging_adapter_c() noexcept override;

  node_logging_adapter_c(const node_logging_adapter_c &) = delete;
  node_logging_adapter_c & operator=(const node_logging_adapter_c &) = delete;
  node_logging_adapter_c(node_logging_adapter_c &&) = delete;
  node_logging_adapter_c & operator=(node_logging_adapter_c &&) = delete;

  void initialize(std::string_view node_name) override;
  void shutdown() noexcept override;
  bool is_initialized() const noexcept override;
  void write(
    base_core::observability::log_level_e level,
    std::string_view message,
    const std::source_location & location) noexcept override;

private:
  std::atomic<bool> m_initialized;
};

}  // namespace logging
