#include "common/cancellable_curl.hpp"

#include <chrono>
#include <new>
#include <stdexcept>

namespace common
{
namespace
{

using namespace std::chrono_literals;

constexpr std::chrono::milliseconds CURL_POLL_TIMEOUT = 1s;

class curl_global_state_c final
{
public:
  curl_global_state_c()
  {
    if (::curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
      throw std::runtime_error("Failed to initialize libcurl");
    }
  }

  ~curl_global_state_c() noexcept
  {
    ::curl_global_cleanup();
  }

  curl_global_state_c(const curl_global_state_c &) = delete;
  curl_global_state_c & operator=(const curl_global_state_c &) = delete;
  curl_global_state_c(curl_global_state_c &&) = delete;
  curl_global_state_c & operator=(curl_global_state_c &&) = delete;
};

void ensure_curl_initialized()
{
  static const curl_global_state_c curl_global_state;
  static_cast<void>(curl_global_state);
}

class performing_guard_c final
{
public:
  performing_guard_c(
    std::atomic_flag & performing,
    std::atomic<bool8_t> & cancel_requested) noexcept
  : m_performing(performing),
    m_cancel_requested(cancel_requested)
  {
  }

  ~performing_guard_c() noexcept
  {
    m_cancel_requested.store(false, std::memory_order_release);
    m_performing.clear(std::memory_order_release);
    m_performing.notify_all();
  }

  performing_guard_c(const performing_guard_c &) = delete;
  performing_guard_c & operator=(const performing_guard_c &) = delete;
  performing_guard_c(performing_guard_c &&) = delete;
  performing_guard_c & operator=(performing_guard_c &&) = delete;

private:
  std::atomic_flag & m_performing;
  std::atomic<bool8_t> & m_cancel_requested;
};

class multi_registration_c final
{
public:
  multi_registration_c(
    CURLM * const multi_handle,
    CURL * const easy_handle) noexcept
  : m_multi_handle(multi_handle),
    m_easy_handle(easy_handle),
    m_registered(m_multi_handle != nullptr && m_easy_handle != nullptr &&
      ::curl_multi_add_handle(m_multi_handle, m_easy_handle) == CURLM_OK)
  {
  }

  ~multi_registration_c() noexcept
  {
    if (m_registered) {
      static_cast<void>(
        ::curl_multi_remove_handle(m_multi_handle, m_easy_handle));
    }
  }

  multi_registration_c(const multi_registration_c &) = delete;
  multi_registration_c & operator=(const multi_registration_c &) = delete;
  multi_registration_c(multi_registration_c &&) = delete;
  multi_registration_c & operator=(multi_registration_c &&) = delete;

  bool is_registered() const noexcept
  {
    return m_registered;
  }

private:
  /// Borrowed opaque handles owned by the surrounding RAII objects.
  CURLM * m_multi_handle;
  CURL * m_easy_handle;
  bool m_registered;
};

}  // namespace

void curl_easy_handle_deleter_s::operator()(CURL * const handle) const noexcept
{
  if (handle != nullptr) {
    ::curl_easy_cleanup(handle);
  }
}

curl_easy_handle_t make_curl_easy_handle()
{
  ensure_curl_initialized();
  curl_easy_handle_t handle(::curl_easy_init());
  if (handle == nullptr) {
    throw std::bad_alloc();
  }
  return handle;
}

curl_headers_c::curl_headers_c() noexcept
: m_headers()
{
}

void curl_headers_c::curl_headers_deleter_s::operator()(
  curl_slist * const headers) const noexcept
{
  if (headers != nullptr) {
    ::curl_slist_free_all(headers);
  }
}

curl_headers_c::~curl_headers_c() noexcept = default;

bool curl_headers_c::add(const std::string & value) noexcept
{
  curl_slist * const current_headers = m_headers.release();
  curl_slist * const updated_headers = ::curl_slist_append(
    current_headers,
    value.c_str());
  if (updated_headers == nullptr) {
    m_headers.reset(current_headers);
    return false;
  }
  m_headers.reset(updated_headers);
  return true;
}

curl_slist * curl_headers_c::get_native_handle() const noexcept
{
  return m_headers.get();
}

void cancellable_curl_c::curl_multi_handle_deleter_s::operator()(
  CURLM * const handle) const noexcept
{
  if (handle != nullptr) {
    static_cast<void>(::curl_multi_cleanup(handle));
  }
}

cancellable_curl_c::cancellable_curl_c()
: m_multi_handle(),
  m_cancel_requested(false)
{
  ensure_curl_initialized();
  m_multi_handle.reset(::curl_multi_init());
  if (m_multi_handle == nullptr) {
    throw std::bad_alloc();
  }
}

cancellable_curl_c::~cancellable_curl_c() noexcept
{
  this->cancel();
  while (m_performing.test(std::memory_order_acquire)) {
    m_performing.wait(true, std::memory_order_acquire);
  }
}

CURLcode cancellable_curl_c::perform(
  const curl_easy_handle_t & easy_handle) noexcept
{
  if (easy_handle == nullptr) {
    return CURLE_FAILED_INIT;
  }
  if (m_performing.test_and_set(std::memory_order_acquire)) {
    return CURLE_FAILED_INIT;
  }
  const performing_guard_c performing_guard(
    m_performing,
    m_cancel_requested);
  if (m_cancel_requested.load(std::memory_order_acquire)) {
    return CURLE_ABORTED_BY_CALLBACK;
  }

  multi_registration_c registration(
    m_multi_handle.get(),
    easy_handle.get());
  if (!registration.is_registered()) {
    return CURLE_FAILED_INIT;
  }

  int32_t running_handles = 0;
  while (!m_cancel_requested.load(std::memory_order_acquire)) {
    if (::curl_multi_perform(m_multi_handle.get(), &running_handles) !=
      CURLM_OK)
    {
      return CURLE_RECV_ERROR;
    }
    if (running_handles == 0) {
      int32_t remaining_messages = 0;
      CURLMsg * const message = ::curl_multi_info_read(
        m_multi_handle.get(),
        &remaining_messages);
      if (message != nullptr && message->msg == CURLMSG_DONE &&
        message->easy_handle == easy_handle.get())
      {
        return message->data.result;
      }
      return CURLE_RECV_ERROR;
    }

    int32_t file_descriptor_count = 0;
    if (::curl_multi_poll(
        m_multi_handle.get(),
        nullptr,
        0U,
        static_cast<int32_t>(CURL_POLL_TIMEOUT.count()),
        &file_descriptor_count) != CURLM_OK)
    {
      return CURLE_RECV_ERROR;
    }
  }
  return CURLE_ABORTED_BY_CALLBACK;
}

void cancellable_curl_c::cancel() noexcept
{
  m_cancel_requested.store(true, std::memory_order_release);
  if (m_multi_handle != nullptr) {
    static_cast<void>(::curl_multi_wakeup(m_multi_handle.get()));
  }
}

}  // namespace common
