#pragma once

#include "common/logging/log_protocol.hpp"
#include "logging/visibility_control.hpp"

#include <filesystem>
#include <memory>

namespace spdlog
{
class async_logger;
namespace details
{
class thread_pool;
}
}

namespace logging
{

class LOGGING_PUBLIC node_log_sink_c final
{
public:
  node_log_sink_c(
    const std::filesystem::path & log_directory,
    const common::logging::log_registration_s & registration,
    const std::shared_ptr<spdlog::details::thread_pool> & thread_pool);
  ~node_log_sink_c() noexcept;

  node_log_sink_c(const node_log_sink_c &) = delete;
  node_log_sink_c & operator=(const node_log_sink_c &) = delete;
  node_log_sink_c(node_log_sink_c &&) = delete;
  node_log_sink_c & operator=(node_log_sink_c &&) = delete;

  void write(const common::logging::log_record_s & record) noexcept;

private:
  std::shared_ptr<spdlog::async_logger> m_logger;
};

}  // namespace logging
