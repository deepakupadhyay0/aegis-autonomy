#pragma once

#include "common/logging/log_protocol.hpp"
#include "logging/visibility_control.hpp"

#include <memory>
#include <string_view>

namespace logging
{

class diagnostic_dds_sink_impl_c;

class LOGGING_PUBLIC diagnostic_dds_sink_c final
{
public:
  diagnostic_dds_sink_c();
  ~diagnostic_dds_sink_c() noexcept;

  diagnostic_dds_sink_c(const diagnostic_dds_sink_c &) = delete;
  diagnostic_dds_sink_c & operator=(const diagnostic_dds_sink_c &) = delete;
  diagnostic_dds_sink_c(diagnostic_dds_sink_c &&) = delete;
  diagnostic_dds_sink_c & operator=(diagnostic_dds_sink_c &&) = delete;

  void add(
    std::string_view node_name,
    const common::logging::log_record_s & record) noexcept;
  void flush_if_due() noexcept;
  void flush() noexcept;

private:
  std::unique_ptr<diagnostic_dds_sink_impl_c> m_impl;
};

}  // namespace logging
