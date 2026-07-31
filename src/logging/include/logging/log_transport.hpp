#pragma once

#include "common/logging/log_protocol.hpp"
#include "logging/visibility_control.hpp"

#include <cstdint>

namespace logging
{

/// @brief Connection boundary owned and called exclusively by the sender thread.
class LOGGING_PUBLIC abstract_log_transport_c
{
public:
  virtual ~abstract_log_transport_c() noexcept = default;

  abstract_log_transport_c(const abstract_log_transport_c &) = delete;
  abstract_log_transport_c & operator=(const abstract_log_transport_c &) = delete;
  abstract_log_transport_c(abstract_log_transport_c &&) = delete;
  abstract_log_transport_c & operator=(abstract_log_transport_c &&) = delete;

  virtual bool connect(
    const common::logging::log_registration_s & registration) noexcept = 0;
  virtual bool send(
    const common::logging::log_record_s & record) noexcept = 0;
  virtual void disconnect() noexcept = 0;

protected:
  abstract_log_transport_c() noexcept = default;
};

/// @brief RAII Unix-domain transport for the standalone logging service.
class LOGGING_PUBLIC unix_log_transport_c final :
  public abstract_log_transport_c
{
public:
  unix_log_transport_c() noexcept;
  ~unix_log_transport_c() noexcept override;

  unix_log_transport_c(const unix_log_transport_c &) = delete;
  unix_log_transport_c & operator=(const unix_log_transport_c &) = delete;
  unix_log_transport_c(unix_log_transport_c &&) = delete;
  unix_log_transport_c & operator=(unix_log_transport_c &&) = delete;

  bool connect(
    const common::logging::log_registration_s & registration) noexcept override;
  bool send(
    const common::logging::log_record_s & record) noexcept override;
  void disconnect() noexcept override;

private:
  int32_t m_socket_fd;
};

}  // namespace logging
