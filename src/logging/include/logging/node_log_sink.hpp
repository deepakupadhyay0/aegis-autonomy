#pragma once

#include "logging/log_types.hpp"
#include "logging/visibility_control.hpp"

#include <memory>
#include <string_view>

namespace spdlog
{
class logger;
}

namespace logging
{

class LOGGING_PUBLIC abstract_log_sink_c
{
public:
  virtual ~abstract_log_sink_c() noexcept = default;

  abstract_log_sink_c(const abstract_log_sink_c &) = delete;
  abstract_log_sink_c & operator=(const abstract_log_sink_c &) = delete;
  abstract_log_sink_c(abstract_log_sink_c &&) = delete;
  abstract_log_sink_c & operator=(abstract_log_sink_c &&) = delete;

  virtual bool write(const log_record_s & record) noexcept = 0;
  virtual bool flush() noexcept = 0;

protected:
  abstract_log_sink_c() noexcept = default;
};

class LOGGING_PUBLIC node_log_sink_c final : public abstract_log_sink_c
{
public:
  node_log_sink_c(
    std::string_view node_name,
    std::string_view log_directory,
    common::uint64_t file_size_bytes,
    common::uint32_t max_files,
    bool enable_console_log);
  ~node_log_sink_c() noexcept override;

  node_log_sink_c(const node_log_sink_c &) = delete;
  node_log_sink_c & operator=(const node_log_sink_c &) = delete;
  node_log_sink_c(node_log_sink_c &&) = delete;
  node_log_sink_c & operator=(node_log_sink_c &&) = delete;

  bool write(const log_record_s & record) noexcept override;
  bool flush() noexcept override;

private:
  std::shared_ptr<spdlog::logger> m_logger;
};

}  // namespace logging
