#pragma once

#include "logging/log_types.hpp"
#include "logging/visibility_control.hpp"

#include <cstdint>
#include <memory>

namespace logging
{

class logging_service_impl_c;

class LOGGING_PUBLIC logging_service_c final
{
public:
  explicit logging_service_c(const logging_service_options_s & options);
  ~logging_service_c() noexcept;

  logging_service_c(const logging_service_c &) = delete;
  logging_service_c & operator=(const logging_service_c &) = delete;
  logging_service_c(logging_service_c &&) = delete;
  logging_service_c & operator=(logging_service_c &&) = delete;

  void execute(const int32_t exit_notification_fd);

private:
  std::unique_ptr<logging_service_impl_c> m_impl;
};

}  // namespace logging
