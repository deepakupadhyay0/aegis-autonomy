#include "logging/logging_service.hpp"

#include "common/logging/log_codec.hpp"
#include "common/process/exit_detection.hpp"
#include "common/resource_names.hpp"
#include "logging/diagnostic_dds_sink.hpp"
#include "logging/node_log_sink.hpp"

#include <spdlog/async.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <poll.h>
#include <pthread.h>
#include <stdexcept>
#include <string_view>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

namespace logging
{

namespace
{

constexpr std::size_t MAX_LOGGING_CLIENTS = 64U;
constexpr std::size_t SERVICE_QUEUE_CAPACITY = 8192U;
constexpr int32_t SERVICE_POLL_TIMEOUT_MS = 50;
constexpr std::size_t LISTENER_POLL_INDEX = 0U;
constexpr std::size_t EXIT_POLL_INDEX = 1U;
constexpr std::size_t SESSION_POLL_OFFSET = 2U;

std::filesystem::path get_log_directory()
{
  const char * const environment_directory =
    std::getenv("AEGIS_AUTONOMY_LOG_DIR");
  const std::filesystem::path log_directory =
    environment_directory != nullptr && environment_directory[0] != '\0' ?
    std::filesystem::path(environment_directory) :
    std::filesystem::path("./logs");

  std::error_code filesystem_error;
  std::filesystem::create_directories(log_directory, filesystem_error);
  if (filesystem_error ||
    !std::filesystem::is_directory(log_directory, filesystem_error) ||
    filesystem_error)
  {
    throw std::runtime_error("Failed to create or access log directory");
  }
  return log_directory;
}

void close_fd(int32_t & fd) noexcept
{
  if (fd >= 0) {
    static_cast<void>(::close(fd));
    fd = -1;
  }
}

int32_t create_listener()
{
  const int32_t fd = ::socket(
    AF_UNIX,
    SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK,
    0);
  if (fd < 0) {
    throw std::runtime_error("Failed to create logging service socket");
  }

  const std::string_view endpoint = aegis_autonomy::resources::LOGGING_SERVICE;
  if (endpoint.empty() || endpoint.front() != '@') {
    static_cast<void>(::close(fd));
    throw std::runtime_error("Logging service endpoint must be abstract");
  }
  const std::string_view abstract_name = endpoint.substr(1U);
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  address.sun_path[0] = '\0';
  if (abstract_name.size() > sizeof(address.sun_path) - 1U) {
    static_cast<void>(::close(fd));
    throw std::runtime_error("Logging service endpoint is too long");
  }
  std::memcpy(
    &address.sun_path[1],
    abstract_name.data(),
    abstract_name.size());
  const socklen_t address_size = static_cast<socklen_t>(
    offsetof(sockaddr_un, sun_path) + 1U + abstract_name.size());
  if (::bind(
      fd,
      reinterpret_cast<const sockaddr *>(&address),
      address_size) < 0 ||
    ::listen(fd, static_cast<int32_t>(MAX_LOGGING_CLIENTS)) < 0)
  {
    static_cast<void>(::close(fd));
    throw std::runtime_error("Failed to bind logging service socket");
  }
  return fd;
}

void set_writer_thread_name() noexcept
{
  static_cast<void>(::pthread_setname_np(::pthread_self(), "log_writer"));
}

struct client_session_s
{
  int32_t fd{-1};
  common::string64_t node_name;
  std::unique_ptr<node_log_sink_c> sink;
  uint64_t last_dropped_record_count{0U};

  bool is_registered() const noexcept
  {
    return sink != nullptr;
  }

  void reset() noexcept
  {
    close_fd(fd);
    sink.reset();
    node_name.clear();
    last_dropped_record_count = 0U;
  }
};

}  // namespace

class logging_service_impl_c final
{
public:
  explicit logging_service_impl_c(const logging_service_options_s & options)
  : m_log_directory(get_log_directory()),
    m_thread_pool(std::make_shared<spdlog::details::thread_pool>(
        SERVICE_QUEUE_CAPACITY,
        1U,
        set_writer_thread_name)),
    m_diagnostic_sink(
      options.enable_diagnostic_dds ?
      std::make_unique<diagnostic_dds_sink_c>() :
      nullptr),
    m_listener_fd(create_listener()),
    m_sessions()
  {
  }

  ~logging_service_impl_c() noexcept
  {
    for (client_session_s & session : m_sessions) {
      session.reset();
    }
    if (m_diagnostic_sink != nullptr) {
      m_diagnostic_sink->flush();
    }
    m_thread_pool.reset();
    close_fd(m_listener_fd);
  }

  void execute(const int32_t exit_notification_fd)
  {
    while (true) {
      std::array<pollfd, MAX_LOGGING_CLIENTS + SESSION_POLL_OFFSET> poll_fds{};
      poll_fds[LISTENER_POLL_INDEX].fd = m_listener_fd;
      poll_fds[LISTENER_POLL_INDEX].events = POLLIN;
      poll_fds[EXIT_POLL_INDEX].fd = exit_notification_fd;
      poll_fds[EXIT_POLL_INDEX].events = POLLIN;
      for (std::size_t index = 0U; index < m_sessions.size(); ++index) {
        poll_fds[index + SESSION_POLL_OFFSET].fd = m_sessions[index].fd;
        poll_fds[index + SESSION_POLL_OFFSET].events = POLLIN;
      }

      const int32_t poll_result = ::poll(
        poll_fds.data(),
        static_cast<nfds_t>(poll_fds.size()),
        SERVICE_POLL_TIMEOUT_MS);
      if (poll_result < 0) {
        if (errno == EINTR) {
          continue;
        }
        throw std::runtime_error("Logging service poll failed");
      }

      const int16_t exit_events = poll_fds[EXIT_POLL_INDEX].revents;
      if ((exit_events & POLLIN) != 0) {
        common::process::exit_notification_s notification;
        ssize_t received_size = -1;
        do {
          received_size = ::read(
            exit_notification_fd,
            &notification,
            sizeof(notification));
        } while (received_size < 0 && errno == EINTR);
        if (received_size == static_cast<ssize_t>(sizeof(notification))) {
          return;
        }
      }
      if ((exit_events & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        return;
      }

      if ((poll_fds[LISTENER_POLL_INDEX].revents & POLLIN) != 0) {
        this->accept_client();
      }
      for (std::size_t index = 0U; index < m_sessions.size(); ++index) {
        const int16_t events =
          poll_fds[index + SESSION_POLL_OFFSET].revents;
        if ((events & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
          m_sessions[index].reset();
        } else if ((events & POLLIN) != 0) {
          this->receive_packet(m_sessions[index]);
        }
      }
      if (m_diagnostic_sink != nullptr) {
        m_diagnostic_sink->flush_if_due();
      }
    }
  }

private:
  void accept_client() noexcept
  {
    int32_t client_fd = -1;
    do {
      client_fd = ::accept4(
        m_listener_fd,
        nullptr,
        nullptr,
        SOCK_CLOEXEC | SOCK_NONBLOCK);
    } while (client_fd < 0 && errno == EINTR);
    if (client_fd < 0) {
      return;
    }

    for (client_session_s & session : m_sessions) {
      if (session.fd < 0) {
        session.fd = client_fd;
        return;
      }
    }
    close_fd(client_fd);
  }

  void receive_packet(client_session_s & session) noexcept
  {
    std::array<
      std::byte,
      common::logging::MAX_LOG_PACKET_WIRE_SIZE> packet{};
    iovec vector{};
    vector.iov_base = packet.data();
    vector.iov_len = packet.size();
    msghdr message{};
    message.msg_iov = &vector;
    message.msg_iovlen = 1U;

    ssize_t received_size = -1;
    do {
      received_size = ::recvmsg(session.fd, &message, MSG_DONTWAIT);
    } while (received_size < 0 && errno == EINTR);
    if (received_size <= 0 || (message.msg_flags & MSG_TRUNC) != 0) {
      if (received_size < 0 &&
        (errno == EAGAIN || errno == EWOULDBLOCK))
      {
        return;
      }
      session.reset();
      return;
    }

    const std::size_t packet_size = static_cast<std::size_t>(received_size);
    const common::logging::log_packet_type_e packet_type =
      common::logging::get_log_packet_type(packet.data(), packet_size);
    if (packet_type == common::logging::log_packet_type_e::registration) {
      this->register_client(session, packet.data(), packet_size);
      return;
    }
    if (packet_type == common::logging::log_packet_type_e::record) {
      this->handle_record(session, packet.data(), packet_size);
      return;
    }
    session.reset();
  }

  void register_client(
    client_session_s & session,
    const std::byte * const packet,
    const std::size_t packet_size) noexcept
  {
    if (session.is_registered()) {
      session.reset();
      return;
    }

    common::logging::log_registration_s registration;
    if (!common::logging::deserialize_log_registration(
        packet, packet_size, registration) ||
      this->is_node_registered(registration.node_name.view()))
    {
      session.reset();
      return;
    }

    try {
      session.sink = std::make_unique<node_log_sink_c>(
        m_log_directory,
        registration,
        m_thread_pool);
      session.node_name = registration.node_name;
    } catch (...) {
      session.reset();
    }
  }

  bool is_node_registered(const std::string_view node_name) const noexcept
  {
    for (const client_session_s & session : m_sessions) {
      if (session.is_registered() && session.node_name == node_name) {
        return true;
      }
    }
    return false;
  }

  void handle_record(
    client_session_s & session,
    const std::byte * const packet,
    const std::size_t packet_size) noexcept
  {
    if (!session.is_registered()) {
      session.reset();
      return;
    }

    common::logging::log_record_s record;
    if (!common::logging::deserialize_log_record(
        packet, packet_size, record))
    {
      session.reset();
      return;
    }

    if (record.dropped_record_count > session.last_dropped_record_count) {
      this->write_drop_notice(session, record);
    }
    session.last_dropped_record_count = record.dropped_record_count;
    session.sink->write(record);
    if (m_diagnostic_sink != nullptr) {
      m_diagnostic_sink->add(session.node_name.view(), record);
    }
  }

  void write_drop_notice(
    client_session_s & session,
    const common::logging::log_record_s & next_record) noexcept
  {
    const uint64_t dropped_count =
      next_record.dropped_record_count -
      session.last_dropped_record_count;
    std::array<char, common::string256_t::capacity() + 1U> message{};
    const int32_t message_size = static_cast<int32_t>(std::snprintf(
        message.data(),
        message.size(),
        "Logger dropped %" PRIu64 " record(s)",
        dropped_count));
    if (message_size <= 0) {
      return;
    }

    common::logging::log_record_s drop_notice;
    drop_notice.timestamp_ns = next_record.timestamp_ns;
    drop_notice.level = common::logging::log_level_e::warning;
    drop_notice.thread_name = "log_sender";
    drop_notice.source_file = "logging_service";
    drop_notice.message.assign(message.data());
    session.sink->write(drop_notice);
    if (m_diagnostic_sink != nullptr) {
      m_diagnostic_sink->add(session.node_name.view(), drop_notice);
    }
  }

  std::filesystem::path m_log_directory;
  std::shared_ptr<spdlog::details::thread_pool> m_thread_pool;
  std::unique_ptr<diagnostic_dds_sink_c> m_diagnostic_sink;
  int32_t m_listener_fd;
  std::array<client_session_s, MAX_LOGGING_CLIENTS> m_sessions;
};

logging_service_c::logging_service_c(
  const logging_service_options_s & options)
: m_impl(std::make_unique<logging_service_impl_c>(options))
{
}

logging_service_c::~logging_service_c() noexcept = default;

void logging_service_c::execute(
  const int32_t exit_notification_fd)
{
  m_impl->execute(exit_notification_fd);
}

}  // namespace logging
