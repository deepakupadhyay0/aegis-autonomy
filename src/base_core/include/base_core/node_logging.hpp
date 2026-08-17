#pragma once

#include "base_core/visibility_control.hpp"
#include "common/numeric_types.hpp"

#include <source_location>
#include <string_view>

namespace base_core
{
namespace observability
{

enum class log_level_e : common::uint8_t
{
  debug = 0U,
  info,
  warning,
  error,
  fatal
};

/// @brief Optional process-logging contract used by base_node_c.
class BASE_CORE_PUBLIC node_logging_c
{
public:
  virtual ~node_logging_c() noexcept = default;

  node_logging_c(const node_logging_c &) = delete;
  node_logging_c & operator=(const node_logging_c &) = delete;
  node_logging_c(node_logging_c &&) = delete;
  node_logging_c & operator=(node_logging_c &&) = delete;

  virtual void initialize(std::string_view node_name) = 0;
  virtual void shutdown() noexcept = 0;
  virtual bool is_initialized() const noexcept = 0;
  virtual void write(
    log_level_e level,
    std::string_view message,
    const std::source_location & location) noexcept = 0;

protected:
  node_logging_c() noexcept = default;
};

}  // namespace observability
}  // namespace base_core
