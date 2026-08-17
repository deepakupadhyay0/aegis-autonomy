#pragma once

#include "common/numeric_types.hpp"

#include <curl/curl.h>

#include <atomic>
#include <memory>
#include <string>

namespace common
{

struct curl_easy_handle_deleter_s
{
  void operator()(CURL * handle) const noexcept;
};

using curl_easy_handle_t =
  std::unique_ptr<CURL, curl_easy_handle_deleter_s>;

curl_easy_handle_t make_curl_easy_handle();

/// @brief RAII owner for a libcurl request-header list.
class curl_headers_c final
{
public:
  curl_headers_c() noexcept;
  ~curl_headers_c() noexcept;

  curl_headers_c(const curl_headers_c &) = delete;
  curl_headers_c & operator=(const curl_headers_c &) = delete;
  curl_headers_c(curl_headers_c &&) = delete;
  curl_headers_c & operator=(curl_headers_c &&) = delete;

  bool add(const std::string & value) noexcept;

  /// @brief Returns a borrowed native handle owned by this object.
  curl_slist * get_native_handle() const noexcept;

private:
  struct curl_headers_deleter_s
  {
    void operator()(curl_slist * headers) const noexcept;
  };

  using curl_headers_handle_t =
    std::unique_ptr<curl_slist, curl_headers_deleter_s>;

  curl_headers_handle_t m_headers;
};

/// @brief Executes one libcurl easy request at a time with external cancel.
class cancellable_curl_c final
{
public:
  cancellable_curl_c();
  ~cancellable_curl_c() noexcept;

  cancellable_curl_c(const cancellable_curl_c &) = delete;
  cancellable_curl_c & operator=(const cancellable_curl_c &) = delete;
  cancellable_curl_c(cancellable_curl_c &&) = delete;
  cancellable_curl_c & operator=(cancellable_curl_c &&) = delete;

  /// @return CURLE_ABORTED_BY_CALLBACK when cancel() interrupts the request.
  CURLcode perform(const curl_easy_handle_t & easy_handle) noexcept;
  void cancel() noexcept;

private:
  struct curl_multi_handle_deleter_s
  {
    void operator()(CURLM * handle) const noexcept;
  };

  using curl_multi_handle_t =
    std::unique_ptr<CURLM, curl_multi_handle_deleter_s>;

  curl_multi_handle_t m_multi_handle;
  std::atomic<bool8_t> m_cancel_requested;
  std::atomic_flag m_performing = ATOMIC_FLAG_INIT;
};

}  // namespace common
