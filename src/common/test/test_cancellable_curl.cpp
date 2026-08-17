#include "common/cancellable_curl.hpp"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <future>
#include <netinet/in.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace
{

using namespace std::chrono_literals;

class socket_handle_c final
{
public:
  explicit socket_handle_c(const int32_t descriptor = -1) noexcept
  : m_descriptor(descriptor)
  {
  }

  ~socket_handle_c() noexcept
  {
    this->reset();
  }

  socket_handle_c(const socket_handle_c &) = delete;
  socket_handle_c & operator=(const socket_handle_c &) = delete;

  socket_handle_c(socket_handle_c && other) noexcept
  : m_descriptor(std::exchange(other.m_descriptor, -1))
  {
  }

  socket_handle_c & operator=(socket_handle_c && other) noexcept
  {
    if (this != &other) {
      this->reset();
      m_descriptor = std::exchange(other.m_descriptor, -1);
    }
    return *this;
  }

  int32_t get() const noexcept
  {
    return m_descriptor;
  }

private:
  void reset() noexcept
  {
    if (m_descriptor >= 0) {
      static_cast<void>(::close(m_descriptor));
      m_descriptor = -1;
    }
  }

  int32_t m_descriptor;
};

enum class server_behavior_e : uint8_t
{
  hold_connection,
  return_success
};

struct listening_socket_s
{
  socket_handle_c socket;
  uint16_t port;
};

listening_socket_s create_listening_socket()
{
  socket_handle_c socket(
    ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
  if (socket.get() < 0) {
    throw std::runtime_error("Failed to create loopback test socket");
  }

  struct sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0U;
  if (::bind(
      socket.get(),
      reinterpret_cast<const struct sockaddr *>(&address),
      sizeof(address)) != 0 ||
    ::listen(socket.get(), 1) != 0)
  {
    throw std::runtime_error("Failed to bind loopback test socket");
  }

  socklen_t address_size = sizeof(address);
  if (::getsockname(
      socket.get(),
      reinterpret_cast<struct sockaddr *>(&address),
      &address_size) != 0)
  {
    throw std::runtime_error("Failed to read loopback test port");
  }

  return listening_socket_s{
    std::move(socket),
    ntohs(address.sin_port)};
}

class local_http_server_c final
{
public:
  explicit local_http_server_c(const server_behavior_e behavior)
  : local_http_server_c(behavior, create_listening_socket())
  {
  }

  ~local_http_server_c() noexcept
  {
    m_thread.request_stop();
    if (m_thread.joinable()) {
      m_thread.join();
    }
  }

  local_http_server_c(const local_http_server_c &) = delete;
  local_http_server_c & operator=(const local_http_server_c &) = delete;
  local_http_server_c(local_http_server_c &&) = delete;
  local_http_server_c & operator=(local_http_server_c &&) = delete;

  std::string url() const
  {
    return "http://127.0.0.1:" + std::to_string(m_port) + "/";
  }

  bool wait_for_connection(const std::chrono::milliseconds timeout) const
  {
    if (m_connection_future.wait_for(timeout) != std::future_status::ready) {
      return false;
    }
    return m_connection_future.get();
  }

private:
  local_http_server_c(
    const server_behavior_e behavior,
    listening_socket_s listening_socket)
  : m_listener(std::move(listening_socket.socket)),
    m_port(listening_socket.port),
    m_behavior(behavior),
    m_connection_promise(),
    m_connection_future(m_connection_promise.get_future().share()),
    m_thread(
      [this](const std::stop_token stop_token) {
        this->serve(stop_token);
      })
  {
  }

  void serve(const std::stop_token stop_token) noexcept
  {
    struct pollfd descriptor {};
    descriptor.fd = m_listener.get();
    descriptor.events = POLLIN;

    while (!stop_token.stop_requested()) {
      const int32_t poll_result = ::poll(&descriptor, 1U, 10);
      if (poll_result < 0 && errno == EINTR) {
        continue;
      }
      if (poll_result < 0) {
        m_connection_promise.set_value(false);
        return;
      }
      if (poll_result == 0 || (descriptor.revents & POLLIN) == 0) {
        continue;
      }

      socket_handle_c client(
        ::accept4(m_listener.get(), nullptr, nullptr, SOCK_CLOEXEC));
      if (client.get() < 0 && errno == EINTR) {
        continue;
      }
      if (client.get() < 0) {
        m_connection_promise.set_value(false);
        return;
      }

      m_connection_promise.set_value(true);
      if (m_behavior == server_behavior_e::return_success) {
        if (this->receive_request(client.get(), stop_token)) {
          this->send_success(client.get());
        }
        return;
      }

      while (!stop_token.stop_requested()) {
        std::this_thread::sleep_for(1ms);
      }
      return;
    }

    m_connection_promise.set_value(false);
  }

  static bool receive_request(
    const int32_t client_descriptor,
    const std::stop_token stop_token) noexcept
  {
    constexpr std::array<char8_t, 4U> HEADER_END{'\r', '\n', '\r', '\n'};
    constexpr std::size_t MAXIMUM_REQUEST_SIZE = 8192U;

    std::array<char8_t, 512U> buffer{};
    std::size_t total_received = 0U;
    std::size_t matched_size = 0U;
    while (!stop_token.stop_requested() &&
      total_received < MAXIMUM_REQUEST_SIZE)
    {
      struct pollfd descriptor {};
      descriptor.fd = client_descriptor;
      descriptor.events = POLLIN;
      const int32_t poll_result = ::poll(&descriptor, 1U, 10);
      if (poll_result < 0 && errno == EINTR) {
        continue;
      }
      if (poll_result < 0 ||
        (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
      {
        return false;
      }
      if (poll_result == 0 || (descriptor.revents & POLLIN) == 0) {
        continue;
      }

      const ssize_t received_size = ::recv(
        client_descriptor,
        buffer.data(),
        buffer.size(),
        0);
      if (received_size < 0 && errno == EINTR) {
        continue;
      }
      if (received_size <= 0) {
        return false;
      }

      const std::size_t received_count =
        static_cast<std::size_t>(received_size);
      total_received += received_count;
      for (std::size_t index = 0U; index < received_count; ++index) {
        if (buffer[index] == HEADER_END[matched_size]) {
          ++matched_size;
          if (matched_size == HEADER_END.size()) {
            return true;
          }
        } else {
          matched_size = buffer[index] == HEADER_END[0U] ? 1U : 0U;
        }
      }
    }
    return false;
  }

  static void send_success(const int32_t client_descriptor) noexcept
  {
    const std::string response =
      "HTTP/1.1 204 No Content\r\n"
      "Content-Length: 0\r\n"
      "Connection: close\r\n\r\n";
    std::size_t sent_size = 0U;
    while (sent_size < response.size()) {
      const ssize_t result = ::send(
        client_descriptor,
        response.data() + sent_size,
        response.size() - sent_size,
        MSG_NOSIGNAL);
      if (result < 0 && errno == EINTR) {
        continue;
      }
      if (result <= 0) {
        return;
      }
      sent_size += static_cast<std::size_t>(result);
    }
  }

  socket_handle_c m_listener;
  uint16_t m_port;
  server_behavior_e m_behavior;
  std::promise<bool> m_connection_promise;
  std::shared_future<bool> m_connection_future;
  std::jthread m_thread;
};

bool configure_request(
  const common::curl_easy_handle_t & handle,
  const std::string & url) noexcept
{
  // libcurl's variadic option ABI requires native long values.
  const bool url_set = ::curl_easy_setopt(
    handle.get(), CURLOPT_URL, url.c_str()) == CURLE_OK;
  const bool signal_option_set = ::curl_easy_setopt(
    handle.get(), CURLOPT_NOSIGNAL, 1L) == CURLE_OK;
  const bool timeout_set = ::curl_easy_setopt(
    handle.get(), CURLOPT_TIMEOUT_MS, 2000L) == CURLE_OK;
  return url_set && signal_option_set && timeout_set;
}

}  // namespace

TEST(CancellableCurlTest, OwnsEasyHandleAndHeaders)
{
  const common::curl_easy_handle_t handle =
    common::make_curl_easy_handle();
  ASSERT_NE(handle, nullptr);

  common::curl_headers_c headers;
  EXPECT_TRUE(headers.add("Content-Type: application/json"));
  EXPECT_NE(headers.get_native_handle(), nullptr);
}

TEST(CancellableCurlTest, ReportsCancellationBeforeExecution)
{
  common::cancellable_curl_c curl;
  const common::curl_easy_handle_t handle =
    common::make_curl_easy_handle();

  curl.cancel();
  EXPECT_EQ(curl.perform(handle), CURLE_ABORTED_BY_CALLBACK);
}

TEST(CancellableCurlTest, CancelsActiveRequestAndRemainsReusable)
{
  common::cancellable_curl_c curl;

  {
    local_http_server_c server(server_behavior_e::hold_connection);
    const common::curl_easy_handle_t handle =
      common::make_curl_easy_handle();
    const std::string request_url = server.url();
    ASSERT_TRUE(configure_request(handle, request_url));

    std::promise<CURLcode> result_promise;
    std::future<CURLcode> result_future = result_promise.get_future();
    std::jthread request_thread(
      [&curl, &handle, promise = std::move(result_promise)]() mutable {
        promise.set_value(curl.perform(handle));
      });

    const bool connected = server.wait_for_connection(1s);
    curl.cancel();
    EXPECT_TRUE(connected);

    const std::future_status cancellation_status =
      result_future.wait_for(1s);
    EXPECT_EQ(cancellation_status, std::future_status::ready);
    if (cancellation_status == std::future_status::ready) {
      EXPECT_EQ(result_future.get(), CURLE_ABORTED_BY_CALLBACK);
    }
  }

  local_http_server_c server(server_behavior_e::return_success);
  const common::curl_easy_handle_t handle =
    common::make_curl_easy_handle();
  const std::string request_url = server.url();
  ASSERT_TRUE(configure_request(handle, request_url));
  EXPECT_EQ(curl.perform(handle), CURLE_OK);
}
